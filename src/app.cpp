#include "app.h"

#include "catalog_resources.h"
#include "model.h"
#include "univ.h"

#include <adw-compat.h>
#include <adapta.h>

#include <json-glib/json-glib.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <random>
#include <utility>

namespace catalog {

static std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return std::tolower(c);
    });
    return s;
}

static std::string initials_of(const std::string &name) {
    std::string out;
    for (char c : name) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            out += static_cast<char>(
                std::toupper(static_cast<unsigned char>(c)));
            if (out.size() == 2)
                break;
        }
    }
    if (out.empty() && !name.empty())
        out = name.substr(0, 1);
    return out;
}

static std::string icon_style_config_path() {
    return std::string(g_get_user_config_dir()) + "/catalog/config.ini";
}

static IconStyle read_icon_style() {
    g_autoptr(GKeyFile) kf = g_key_file_new();
    if (g_key_file_load_from_file(kf, icon_style_config_path().c_str(),
                                  G_KEY_FILE_NONE, nullptr)) {
        g_autofree char *value =
            g_key_file_get_string(kf, "ui", "icon-style", nullptr);
        if (value) {
            std::string v = lower(value);
            if (v == "kde")
                return IconStyle::Kde;
            if (v == "system")
                return IconStyle::System;
        }
    }
    return IconStyle::Gnome;
}

static void write_icon_style(IconStyle style) {
    g_autoptr(GKeyFile) kf = g_key_file_new();
    const char *value = style == IconStyle::Kde
                            ? "kde"
                            : (style == IconStyle::System ? "system"
                                                          : "gnome");
    g_key_file_set_string(kf, "ui", "icon-style", value);
    std::string dir = std::string(g_get_user_config_dir()) + "/catalog";
    g_mkdir_with_parents(dir.c_str(), 0700);
    g_autofree char *data = g_key_file_to_data(kf, nullptr, nullptr);
    g_file_set_contents(icon_style_config_path().c_str(), data, -1, nullptr);
}

static const char *icon_theme_name(IconStyle style) {
    switch (style) {
    case IconStyle::Gnome:
        return "Adwaita";
    case IconStyle::Kde:
        return "breeze";
    default:
        return nullptr;
    }
}

static std::string row_meta(const Package &p) {
    std::string out;
    auto append = [&out](const std::string &part) {
        if (part.empty())
            return;
        if (!out.empty())
            out += " · ";
        out += part;
    };
    append(p.kind);
    append(p.architecture);
    append(p.repo);
    return out;
}

static GtkWidget *make_row(const Package &p) {
    AdwActionRow *row = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), p.name.c_str());
    adw_action_row_set_subtitle(
        row, p.description.empty() ? "No description" : p.description.c_str());

    std::string initials = initials_of(p.name);
    GtkWidget *avatar = adw_avatar_new(32, initials.c_str(), TRUE);
    adw_action_row_add_prefix(row, avatar);

    GtkWidget *trail = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_halign(trail, GTK_ALIGN_END);
    gtk_widget_set_valign(trail, GTK_ALIGN_CENTER);
    GtkWidget *ver = gtk_label_new(p.version.c_str());
    gtk_widget_add_css_class(ver, "dim-label");
    GtkWidget *meta = gtk_label_new(row_meta(p).c_str());
    gtk_widget_add_css_class(meta, "caption");
    gtk_widget_add_css_class(meta, "dim-label");
    gtk_box_append(GTK_BOX(trail), ver);
    gtk_box_append(GTK_BOX(trail), meta);
    adw_action_row_add_suffix(row, trail);

    return GTK_WIDGET(row);
}

static void clear_list(GtkListBox *list) {
    GtkListBoxRow *row;
    while ((row = gtk_list_box_get_row_at_index(list, 0)) != nullptr)
        gtk_list_box_remove(list, GTK_WIDGET(row));
}

static void append_repos_from_file(
    const std::string &path, const std::string &kind,
    std::vector<std::pair<std::string, std::string>> &out) {
    gchar *contents = nullptr;
    GError *error = nullptr;
    if (!g_file_get_contents(path.c_str(), &contents, nullptr, &error)) {
        g_clear_error(&error);
        return;
    }
    char **lines = g_strsplit(contents, "\n", -1);
    for (char **line = lines; *line; line++) {
        char *trimmed = g_strstrip(*line);
        if (*trimmed == '\0' || *trimmed == '#')
            continue;
        char **tok = g_strsplit_set(trimmed, " \t", -1);
        if (tok[0] && *tok[0])
            out.emplace_back(tok[0], kind);
        g_strfreev(tok);
    }
    g_strfreev(lines);
    g_free(contents);
}

static void append_repo_config(const std::string &kind,
                               const std::string &line) {
    std::string dir = std::string(g_get_home_dir()) + "/.local/univ";
    g_mkdir_with_parents(dir.c_str(), 0755);
    std::string path = dir + (kind == "deb" ? "/debrepos.conf" : "/rpmrepos.conf");
    g_autofree char *contents = nullptr;
    if (g_file_get_contents(path.c_str(), &contents, nullptr, nullptr)) {
        std::string updated = contents;
        if (!updated.empty() && updated.back() != '\n')
            updated += '\n';
        updated += line + "\n";
        g_file_set_contents(path.c_str(), updated.c_str(), -1, nullptr);
    } else {
        g_file_set_contents(path.c_str(), (line + "\n").c_str(), -1, nullptr);
    }
}

static void remove_repo_from_file(const std::string &path,
                                  const std::string &name) {
    g_autofree char *contents = nullptr;
    if (!g_file_get_contents(path.c_str(), &contents, nullptr, nullptr))
        return;
    std::string out;
    char **lines = g_strsplit(contents, "\n", -1);
    for (char **line = lines; *line; line++) {
        char *trimmed = g_strstrip(*line);
        if (*trimmed == '\0' || *trimmed == '#') {
            out += std::string(trimmed) + "\n";
            continue;
        }
        char **tok = g_strsplit_set(trimmed, " \t", -1);
        bool match = tok[0] && name == tok[0];
        g_strfreev(tok);
        if (!match)
            out += std::string(trimmed) + "\n";
    }
    g_strfreev(lines);
    g_file_set_contents(path.c_str(), out.c_str(), -1, nullptr);
}

static void shuffle_packages(std::vector<Package> &packages) {
    static std::mt19937 rng([] {
        std::random_device rd;
        return rd() ^ static_cast<unsigned>(
                          std::chrono::high_resolution_clock::now()
                              .time_since_epoch()
                              .count());
    }());
    std::shuffle(packages.begin(), packages.end(), rng);
}

static void dedupe_packages(std::vector<Package> &list) {
    std::vector<Package> out;
    for (const Package &p : list) {
        bool seen = false;
        for (const Package &q : out)
            if (q.name == p.name) {
                seen = true;
                break;
            }
        if (!seen)
            out.push_back(p);
    }
    list = std::move(out);
}

static std::string featured_cache_path() {
    return std::string(g_get_user_cache_dir()) + "/catalog/featured.json";
}

static void write_featured_cache(const std::vector<Package> &packages) {
    JsonBuilder *builder = json_builder_new();
    json_builder_begin_array(builder);
    for (const Package &p : packages) {
        json_builder_begin_object(builder);
        json_builder_set_member_name(builder, "name");
        json_builder_add_string_value(builder, p.name.c_str());
        json_builder_set_member_name(builder, "version");
        json_builder_add_string_value(builder, p.version.c_str());
        json_builder_set_member_name(builder, "architecture");
        json_builder_add_string_value(builder, p.architecture.c_str());
        json_builder_set_member_name(builder, "description");
        json_builder_add_string_value(builder, p.description.c_str());
        json_builder_set_member_name(builder, "depends");
        json_builder_add_string_value(builder, p.depends.c_str());
        json_builder_set_member_name(builder, "kind");
        json_builder_add_string_value(builder, p.kind.c_str());
        json_builder_set_member_name(builder, "repo");
        json_builder_add_string_value(builder, p.repo.c_str());
        json_builder_end_object(builder);
    }
    json_builder_end_array(builder);
    JsonNode *root = json_builder_get_root(builder);
    if (root) {
        g_autofree char *text = json_to_string(root, FALSE);
        std::string dir = g_get_user_cache_dir();
        std::string subdir = dir + "/catalog";
        g_mkdir_with_parents(subdir.c_str(), 0700);
        g_file_set_contents(featured_cache_path().c_str(), text, -1, nullptr);
        json_node_free(root);
    }
    g_object_unref(builder);
}

static std::vector<Package> read_featured_cache() {
    std::vector<Package> out;
    g_autofree char *contents = nullptr;
    if (!g_file_get_contents(featured_cache_path().c_str(), &contents, nullptr,
                             nullptr))
        return out;
    std::string error;
    parse_packages(contents, out, error);
    return out;
}

enum class Distro { None, Fedora, Debian };

static Distro distro_of(const std::string &name) {
    std::string s = lower(name);
    if (s.find("fedora") != std::string::npos)
        return Distro::Fedora;
    if (s.find("debian") != std::string::npos)
        return Distro::Debian;
    return Distro::None;
}

static const char *distro_logo(Distro d) {
    switch (d) {
    case Distro::Fedora:
        return "/images/fedora-icon-seeklogo.png";
    case Distro::Debian:
        return "/images/debian-seeklogo.png";
    default:
        return nullptr;
    }
}

static void set_featured_colors(const std::vector<GtkWidget *> &banners) {
    if (banners.empty())
        return;
    static std::mt19937 rng([] {
        std::random_device rd;
        return rd() ^ static_cast<unsigned>(
                          std::chrono::high_resolution_clock::now()
                              .time_since_epoch()
                              .count());
    }());
    std::uniform_real_distribution<double> hue_dist(0.0, 360.0);

    std::string css;
    for (size_t i = 0; i < banners.size(); i++) {
        int hue = static_cast<int>(std::llround(hue_dist(rng)));
        css += ".featured-banner-" + std::to_string(i) +
               " { background-color: hsl(" + std::to_string(hue) +
               ", 70%, 40%); }\n";
    }
    css += ".featured-text { color: #ffffff; }\n"
           ".featured-meta { color: rgba(255, 255, 255, 0.78); }\n";

    static GtkCssProvider *provider = gtk_css_provider_new();
    static gboolean added = FALSE;
    if (!added) {
        gtk_style_context_add_provider_for_display(
            gtk_widget_get_display(banners[0]),
            GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        added = TRUE;
    }
    gtk_css_provider_load_from_string(provider, css.c_str());
}

static GtkWidget *make_featured_banner(const Package &p) {
    GtkWidget *btn = gtk_button_new();
    gtk_widget_add_css_class(btn, "card");
    gtk_widget_set_hexpand(btn, TRUE);
    gtk_widget_set_vexpand(btn, TRUE);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 24);
    gtk_widget_set_margin_start(box, 32);
    gtk_widget_set_margin_end(box, 32);
    gtk_widget_set_margin_top(box, 24);
    gtk_widget_set_margin_bottom(box, 24);

    GtkWidget *text = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_hexpand(text, TRUE);
    gtk_widget_set_vexpand(text, TRUE);
    gtk_widget_set_valign(text, GTK_ALIGN_FILL);

    GtkWidget *name = gtk_label_new(p.name.c_str());
    gtk_widget_add_css_class(name, "featured-text");
    gtk_widget_add_css_class(name, "title-1");
    gtk_label_set_xalign(GTK_LABEL(name), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(name), PANGO_ELLIPSIZE_END);

    GtkWidget *desc = gtk_label_new(
        p.description.empty() ? "No description" : p.description.c_str());
    gtk_widget_add_css_class(desc, "featured-text");
    gtk_widget_add_css_class(desc, "body");
    gtk_widget_set_vexpand(desc, TRUE);
    gtk_label_set_xalign(GTK_LABEL(desc), 0.0f);
    gtk_label_set_wrap(GTK_LABEL(desc), TRUE);
    gtk_label_set_ellipsize(GTK_LABEL(desc), PANGO_ELLIPSIZE_END);

    GtkWidget *meta = gtk_label_new(row_meta(p).c_str());
    gtk_widget_add_css_class(meta, "featured-meta");
    gtk_widget_add_css_class(meta, "caption");
    gtk_widget_add_css_class(meta, "dim-label");
    gtk_widget_set_valign(meta, GTK_ALIGN_END);
    gtk_label_set_xalign(GTK_LABEL(meta), 0.0f);

    gtk_box_append(GTK_BOX(text), name);
    gtk_box_append(GTK_BOX(text), desc);
    gtk_box_append(GTK_BOX(text), meta);

    gtk_box_append(GTK_BOX(box), text);

    GtkWidget *child = box;
    if (const char *logo = distro_logo(distro_of(p.repo))) {
        gtk_widget_set_margin_end(text, 88);
        GtkWidget *overlay = gtk_overlay_new();
        gtk_overlay_set_child(GTK_OVERLAY(overlay), box);
        GtkWidget *image = gtk_image_new_from_resource(logo);
        gtk_image_set_pixel_size(GTK_IMAGE(image), 80);
        gtk_widget_set_halign(image, GTK_ALIGN_END);
        gtk_widget_set_valign(image, GTK_ALIGN_START);
        gtk_widget_set_margin_top(image, 24);
        gtk_widget_set_margin_end(image, 24);
        gtk_overlay_add_overlay(GTK_OVERLAY(overlay), image);
        child = overlay;
    }
    gtk_button_set_child(GTK_BUTTON(btn), child);
    return btn;
}

static void ensure_repo_styles(GtkWidget *anchor) {
    static gboolean added = FALSE;
    if (added)
        return;
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_string(
        provider,
        ".repo-fedora { background-color: #57708F; }\n"
        ".repo-debian { background-color: #58111A; }\n"
        ".repo-text { color: #ffffff; }\n"
        ".repo-sub { color: rgba(255, 255, 255, 0.8); }\n"
        "entry.search.catalog-noresults > image:last-child {\n"
        "  color: #e04545;\n"
        "}\n");
    gtk_style_context_add_provider_for_display(
        gtk_widget_get_display(anchor), GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    added = TRUE;
}

static GtkWidget *make_repo_button(const std::string &title,
                                   const std::string &subtitle,
                                   const std::string &repo) {
    Distro distro = distro_of(repo);
    GtkWidget *btn = gtk_button_new();
    gtk_widget_add_css_class(btn, "card");
    if (distro == Distro::Fedora)
        gtk_widget_add_css_class(btn, "repo-fedora");
    else if (distro == Distro::Debian)
        gtk_widget_add_css_class(btn, "repo-debian");
    gtk_widget_set_size_request(btn, 210, -1);

    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_margin_start(row, 12);
    gtk_widget_set_margin_end(row, 12);
    gtk_widget_set_margin_top(row, 10);
    gtk_widget_set_margin_bottom(row, 10);

    const char *logo = distro_logo(distro);
    if (logo) {
        GtkWidget *image = gtk_image_new_from_resource(logo);
        gtk_image_set_pixel_size(GTK_IMAGE(image), 32);
        gtk_widget_set_valign(image, GTK_ALIGN_CENTER);
        gtk_box_append(GTK_BOX(row), image);
    }

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    GtkWidget *name = gtk_label_new(title.c_str());
    gtk_widget_add_css_class(name, "title-3");
    if (distro != Distro::None)
        gtk_widget_add_css_class(name, "repo-text");
    gtk_label_set_xalign(GTK_LABEL(name), 0.5f);
    gtk_label_set_ellipsize(GTK_LABEL(name), PANGO_ELLIPSIZE_END);

    GtkWidget *sub = gtk_label_new(subtitle.c_str());
    gtk_widget_add_css_class(sub, "caption");
    gtk_widget_add_css_class(sub, "dim-label");
    if (distro != Distro::None)
        gtk_widget_add_css_class(sub, "repo-sub");
    gtk_label_set_xalign(GTK_LABEL(sub), 0.5f);

    gtk_box_append(GTK_BOX(box), name);
    gtk_box_append(GTK_BOX(box), sub);
    gtk_box_append(GTK_BOX(row), box);
    gtk_button_set_child(GTK_BUTTON(btn), row);
    return btn;
}

void CatalogWindow::DetailPane::build() {
    stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(stack),
                                  GTK_STACK_TRANSITION_TYPE_CROSSFADE);

    GtkWidget *empty = gtk_label_new("Select a package");
    gtk_widget_add_css_class(empty, "dim-label");
    gtk_widget_set_halign(empty, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(empty, GTK_ALIGN_CENTER);

    GtkWidget *clamp = adw_clamp_new();
    adw_clamp_set_maximum_size(ADW_CLAMP(clamp), 620);
    adw_clamp_set_tightening_threshold(ADW_CLAMP(clamp), 420);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);

    name = gtk_label_new("");
    gtk_widget_add_css_class(name, "title-1");
    gtk_label_set_wrap(GTK_LABEL(name), TRUE);
    gtk_label_set_xalign(GTK_LABEL(name), 0.0f);
    gtk_widget_set_halign(name, GTK_ALIGN_START);

    version = gtk_label_new("");
    gtk_widget_add_css_class(version, "dim-label");
    gtk_widget_set_halign(version, GTK_ALIGN_START);

    meta = gtk_label_new("");
    gtk_widget_add_css_class(meta, "caption");
    gtk_widget_add_css_class(meta, "dim-label");
    gtk_widget_set_halign(meta, GTK_ALIGN_START);

    description = gtk_label_new("");
    gtk_widget_add_css_class(description, "body");
    gtk_label_set_wrap(GTK_LABEL(description), TRUE);
    gtk_label_set_selectable(GTK_LABEL(description), TRUE);
    gtk_label_set_xalign(GTK_LABEL(description), 0.0f);

    GtkWidget *separator = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);

    GtkWidget *buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(buttons, GTK_ALIGN_START);
    install_btn = gtk_button_new_with_label("Install");
    gtk_widget_add_css_class(install_btn, "suggested-action");
    uninstall_btn = gtk_button_new_with_label("Uninstall");
    gtk_widget_add_css_class(uninstall_btn, "destructive-action");
    gtk_widget_set_sensitive(uninstall_btn, FALSE);
    gtk_box_append(GTK_BOX(buttons), install_btn);
    gtk_box_append(GTK_BOX(buttons), uninstall_btn);

    gtk_box_append(GTK_BOX(box), name);
    gtk_box_append(GTK_BOX(box), version);
    gtk_box_append(GTK_BOX(box), meta);
    gtk_box_append(GTK_BOX(box), separator);
    gtk_box_append(GTK_BOX(box), description);
    gtk_box_append(GTK_BOX(box), buttons);

    adw_clamp_set_child(ADW_CLAMP(clamp), box);
    gtk_widget_set_margin_start(clamp, 24);
    gtk_widget_set_margin_end(clamp, 24);
    gtk_widget_set_margin_top(clamp, 16);
    gtk_widget_set_margin_bottom(clamp, 16);

    GtkWidget *content = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(content),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_has_frame(GTK_SCROLLED_WINDOW(content), FALSE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(content), clamp);

    gtk_stack_add_named(GTK_STACK(stack), empty, "empty");
    gtk_stack_add_named(GTK_STACK(stack), content, "content");
    gtk_stack_set_visible_child_name(GTK_STACK(stack), "empty");

    deps_empty = gtk_label_new("No dependencies");
    gtk_widget_add_css_class(deps_empty, "dim-label");
    gtk_widget_set_halign(deps_empty, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(deps_empty, 12);
    gtk_widget_set_visible(deps_empty, TRUE);

    deps_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(deps_list),
                                    GTK_SELECTION_SINGLE);
    gtk_list_box_set_placeholder(GTK_LIST_BOX(deps_list), deps_empty);

    GtkWidget *deps_scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(deps_scroll),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_has_frame(GTK_SCROLLED_WINDOW(deps_scroll), FALSE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(deps_scroll), deps_list);

    GtkWidget *deps_heading = gtk_label_new("Depends on");
    gtk_widget_add_css_class(deps_heading, "heading");
    gtk_widget_set_halign(deps_heading, GTK_ALIGN_START);
    gtk_widget_set_margin_top(deps_heading, 10);
    gtk_widget_set_margin_start(deps_heading, 12);
    gtk_widget_set_margin_end(deps_heading, 12);
    gtk_widget_set_margin_bottom(deps_heading, 4);

    GtkWidget *deps_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_append(GTK_BOX(deps_box), deps_heading);
    gtk_box_append(GTK_BOX(deps_box), deps_scroll);

    paned = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
    gtk_paned_set_start_child(GTK_PANED(paned), stack);
    gtk_paned_set_end_child(GTK_PANED(paned), deps_box);
    gtk_paned_set_wide_handle(GTK_PANED(paned), FALSE);
    gtk_paned_set_position(GTK_PANED(paned), 513);
}

void CatalogWindow::DetailPane::show_deps(const std::string &depends) {
    clear_list(GTK_LIST_BOX(deps_list));
    if (depends.empty())
        return;

    std::string::size_type start = 0;
    while (start <= depends.size()) {
        std::string::size_type comma = depends.find(',', start);
        if (comma == std::string::npos)
            comma = depends.size();
        std::string item = depends.substr(start, comma - start);
        start = comma + 1;
        while (!item.empty() && item.front() == ' ')
            item.erase(item.begin());
        while (!item.empty() && item.back() == ' ')
            item.pop_back();
        if (item.empty())
            continue;

        GtkWidget *row = gtk_list_box_row_new();
        GtkWidget *label = gtk_label_new(item.c_str());
        gtk_widget_add_css_class(label, "body");
        gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
        gtk_widget_set_margin_start(label, 12);
        gtk_widget_set_margin_end(label, 12);
        gtk_widget_set_margin_top(label, 4);
        gtk_widget_set_margin_bottom(label, 4);
        gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(row), TRUE);
        gtk_list_box_row_set_selectable(GTK_LIST_BOX_ROW(row), TRUE);
        g_object_set_data_full(G_OBJECT(row), "dep", g_strdup(item.c_str()),
                               g_free);
        gtk_list_box_append(GTK_LIST_BOX(deps_list), row);
    }
}

void CatalogWindow::DetailPane::show_package(const Package &p) {
    gtk_stack_set_visible_child_name(GTK_STACK(stack), "content");
    gtk_label_set_text(GTK_LABEL(name), p.name.c_str());
    std::string vtext =
        p.version.empty() ? "Unknown version" : ("Version " + p.version);
    gtk_label_set_text(GTK_LABEL(version), vtext.c_str());
    gtk_label_set_text(GTK_LABEL(meta), row_meta(p).c_str());
    gtk_label_set_text(
        GTK_LABEL(description),
        p.description.empty() ? "No description available."
                              : p.description.c_str());
    show_deps(p.depends);
}

void CatalogWindow::DetailPane::clear() {
    gtk_stack_set_visible_child_name(GTK_STACK(stack), "empty");
    gtk_widget_set_sensitive(install_btn, FALSE);
    gtk_widget_set_sensitive(uninstall_btn, FALSE);
    clear_list(GTK_LIST_BOX(deps_list));
}

void CatalogWindow::on_installed_selected(GtkListBox *, GtkListBoxRow *row,
                                          gpointer data) {
    auto *self = static_cast<CatalogWindow *>(data);
    self->installed_selected_ = -1;
    if (row) {
        gpointer idx = g_object_get_data(G_OBJECT(row), "index");
        if (idx)
            self->installed_selected_ = GPOINTER_TO_INT(idx);
    }
    if (self->installed_selected_ >= 0 &&
        self->installed_selected_ < static_cast<int>(self->installed_.size()))
        self->installed_detail_.show_package(
            self->installed_[self->installed_selected_]);
    else
        self->installed_detail_.clear();
    self->update_buttons();
}

void CatalogWindow::on_browse_selected(GtkListBox *, GtkListBoxRow *row,
                                       gpointer data) {
    auto *self = static_cast<CatalogWindow *>(data);
    self->browse_selected_ = -1;
    if (row) {
        gpointer idx = g_object_get_data(G_OBJECT(row), "index");
        if (idx)
            self->browse_selected_ = GPOINTER_TO_INT(idx);
    }
    if (self->browse_selected_ >= 0 &&
        self->browse_selected_ < static_cast<int>(self->browse_.size()))
        self->browse_detail_.show_package(self->browse_[self->browse_selected_]);
    else
        self->browse_detail_.clear();
    self->update_buttons();
}

void CatalogWindow::on_icon_style_toggled(GtkCheckButton *button, gpointer data) {
    auto *self = static_cast<CatalogWindow *>(data);
    if (!gtk_check_button_get_active(button))
        return;
    IconStyle style = static_cast<IconStyle>(GPOINTER_TO_INT(
        g_object_get_data(G_OBJECT(button), "style")));
    if (style == self->icon_style_)
        return;
    self->icon_style_ = style;
    write_icon_style(style);
    self->apply_icon_style();
    const char *label = style == IconStyle::Kde
                            ? "KDE (Breeze)"
                            : (style == IconStyle::System ? "Follow system"
                                                          : "GNOME (Adwaita)");
    self->toast("Icon style: " + std::string(label));
}

void CatalogWindow::on_installed_filter_changed(GtkSearchEntry *entry,
                                                gpointer data) {
    auto *self = static_cast<CatalogWindow *>(data);
    self->installed_filter_ = gtk_editable_get_text(GTK_EDITABLE(entry));
    self->installed_selected_ = -1;
    self->installed_detail_.clear();
    self->populate_installed();
    self->update_buttons();
}

void CatalogWindow::on_browse_search_changed(GtkSearchEntry *entry,
                                             gpointer data) {
    auto *self = static_cast<CatalogWindow *>(data);
    self->browse_query_ = gtk_editable_get_text(GTK_EDITABLE(entry));
    if (self->browse_timeout_)
        g_source_remove(self->browse_timeout_);
    self->browse_timeout_ = g_timeout_add(300, [](gpointer d) -> gboolean {
        auto *self = static_cast<CatalogWindow *>(d);
        self->browse_timeout_ = 0;
        self->do_search();
        return G_SOURCE_REMOVE;
    }, self);
}

void CatalogWindow::on_browse_search_activate(GtkSearchEntry *, gpointer data) {
    auto *self = static_cast<CatalogWindow *>(data);
    if (self->browse_timeout_)
        g_source_remove(self->browse_timeout_);
    self->browse_timeout_ = 0;
    self->do_search();
}

void CatalogWindow::on_featured_clicked(GtkButton *button, gpointer) {
    auto *self = static_cast<CatalogWindow *>(
        g_object_get_data(G_OBJECT(button), "window"));
    const char *name = static_cast<const char *>(
        g_object_get_data(G_OBJECT(button), "package"));
    self->open_browse(name, "");
}

void CatalogWindow::on_repo_clicked(GtkButton *button, gpointer) {
    auto *self = static_cast<CatalogWindow *>(
        g_object_get_data(G_OBJECT(button), "window"));
    const char *repo = static_cast<const char *>(
        g_object_get_data(G_OBJECT(button), "repo"));
    self->open_browse("", repo);
}

void CatalogWindow::on_add_repo_clicked(GtkButton *button, gpointer) {
    auto *self = static_cast<CatalogWindow *>(
        g_object_get_data(G_OBJECT(button), "window"));
    self->show_add_repo_dialog();
}

void CatalogWindow::on_add_repo_submit(GtkButton *button, gpointer) {
    GtkWidget *dialog =
        GTK_WIDGET(gtk_widget_get_root(GTK_WIDGET(button)));
    auto *self = static_cast<CatalogWindow *>(
        g_object_get_data(G_OBJECT(dialog), "window"));
    const char *name = gtk_editable_get_text(GTK_EDITABLE(
        g_object_get_data(G_OBJECT(dialog), "name")));
    const char *url = gtk_editable_get_text(GTK_EDITABLE(
        g_object_get_data(G_OBJECT(dialog), "url")));
    const char *kind = static_cast<const char *>(
        g_object_get_data(G_OBJECT(dialog), "kind"));

    if (!*name || !*url) {
        self->toast("Name and URL are required");
        return;
    }
    append_repo_config(kind, std::string(name) + " " + url);
    self->populate_repos();
    self->populate_repos_page();
    self->toast("Added " + std::string(name));
    gtk_window_destroy(GTK_WINDOW(dialog));
}

void CatalogWindow::show_add_repo_dialog() {
    GtkWidget *dialog = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(dialog), "Add repository");
    gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(window_));
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 420, -1);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_start(box, 16);
    gtk_widget_set_margin_end(box, 16);
    gtk_widget_set_margin_top(box, 16);
    gtk_widget_set_margin_bottom(box, 16);

    auto add_field = [&](const char *text, const char *placeholder,
                         GtkWidget **entry) {
        GtkWidget *label = gtk_label_new(text);
        gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
        GtkWidget *field = gtk_entry_new();
        gtk_entry_set_placeholder_text(GTK_ENTRY(field), placeholder);
        gtk_box_append(GTK_BOX(box), label);
        gtk_box_append(GTK_BOX(box), field);
        *entry = field;
    };

    GtkWidget *name_entry = nullptr;
    GtkWidget *url_entry = nullptr;
    add_field("Name", "e.g. ubuntu", &name_entry);
    add_field("Repository URL", "https://…", &url_entry);

    GtkWidget *kind_label = gtk_label_new("Type");
    gtk_label_set_xalign(GTK_LABEL(kind_label), 0.0f);
    gtk_box_append(GTK_BOX(box), kind_label);

    static const char *kinds[] = {"deb", "rpm", nullptr};
    GtkWidget *kind_drop = gtk_drop_down_new_from_strings(kinds);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(kind_drop), 0);
    gtk_box_append(GTK_BOX(box), kind_drop);

    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(actions, GTK_ALIGN_END);
    gtk_widget_set_margin_top(actions, 8);
    GtkWidget *cancel = gtk_button_new_with_label("Cancel");
    GtkWidget *add = gtk_button_new_with_label("Add");
    gtk_widget_add_css_class(add, "suggested-action");
    gtk_box_append(GTK_BOX(actions), cancel);
    gtk_box_append(GTK_BOX(actions), add);
    gtk_box_append(GTK_BOX(box), actions);

    gtk_window_set_child(GTK_WINDOW(dialog), box);

    g_object_set_data(G_OBJECT(dialog), "window", this);
    g_object_set_data(G_OBJECT(dialog), "name", name_entry);
    g_object_set_data(G_OBJECT(dialog), "url", url_entry);
    g_object_set_data(G_OBJECT(dialog), "kind", const_cast<char *>(kinds[0]));
    g_signal_connect(
        kind_drop, "notify::selected",
        G_CALLBACK(+[](GObject *obj, GParamSpec *, gpointer) {
            guint sel = gtk_drop_down_get_selected(GTK_DROP_DOWN(obj));
            GtkWidget *dlg = GTK_WIDGET(
                gtk_widget_get_root(GTK_WIDGET(obj)));
            g_object_set_data(
                G_OBJECT(dlg), "kind",
                const_cast<char *>((sel == 0) ? "deb" : "rpm"));
        }),
        nullptr);
    g_signal_connect(cancel, "clicked",
                     G_CALLBACK(+[](GtkButton *, gpointer data) {
                         gtk_window_destroy(GTK_WINDOW(data));
                     }),
                     dialog);
    g_signal_connect(add, "clicked", G_CALLBACK(on_add_repo_submit), nullptr);

    gtk_window_present(GTK_WINDOW(dialog));
}

void CatalogWindow::on_refresh_clicked(GtkButton *, gpointer data) {
    static_cast<CatalogWindow *>(data)->refresh_installed();
}

void CatalogWindow::on_update_clicked(GtkButton *, gpointer data) {
    static_cast<CatalogWindow *>(data)->upgrade_all();
}

gboolean CatalogWindow::on_key_pressed(GtkEventControllerKey *, guint keyval,
                                       guint, GdkModifierType state,
                                       gpointer data) {
    auto *self = static_cast<CatalogWindow *>(data);

    if (self->init_screen_ && gtk_widget_get_visible(self->init_screen_))
        return GDK_EVENT_STOP;

    if (state & (GDK_CONTROL_MASK | GDK_ALT_MASK | GDK_SUPER_MASK |
                 GDK_META_MASK))
        return GDK_EVENT_PROPAGATE;

    gunichar ch = gdk_keyval_to_unicode(keyval);
    if (ch == 0 || !g_unichar_isgraph(ch))
        return GDK_EVENT_PROPAGATE;

    GtkWidget *focus = gtk_window_get_focus(GTK_WINDOW(self->window_));
    if (focus && GTK_IS_EDITABLE(focus))
        return GDK_EVENT_PROPAGATE;

    if (!self->browse_search_)
        return GDK_EVENT_PROPAGATE;

    adw_view_stack_set_visible_child_name(ADW_VIEW_STACK(self->stack_),
                                          "browse");
    gtk_widget_grab_focus(self->browse_search_);

    GtkEditable *editable = GTK_EDITABLE(self->browse_search_);
    gtk_editable_set_position(editable, -1);
    char buf[8];
    int len = g_unichar_to_utf8(ch, buf);
    int position = gtk_editable_get_position(editable);
    gtk_editable_insert_text(editable, buf, len, &position);
    gtk_editable_set_position(editable, position);
    return GDK_EVENT_STOP;
}

void CatalogWindow::on_installed_install_clicked(GtkButton *, gpointer data) {
    static_cast<CatalogWindow *>(data)->installed_install();
}

void CatalogWindow::on_installed_uninstall_clicked(GtkButton *, gpointer data) {
    static_cast<CatalogWindow *>(data)->installed_uninstall();
}

void CatalogWindow::on_browse_install_clicked(GtkButton *, gpointer data) {
    static_cast<CatalogWindow *>(data)->browse_install();
}

void CatalogWindow::on_browse_uninstall_clicked(GtkButton *, gpointer data) {
    static_cast<CatalogWindow *>(data)->browse_uninstall();
}

std::string CatalogWindow::dep_name(const std::string &item) {
    std::string name;
    for (char c : item) {
        if (c == ' ' || c == '(')
            break;
        if (c == '|')
            break;
        if (c == ':')
            break;
        name.push_back(c);
    }
    return name;
}

void CatalogWindow::on_dep_selected(GtkListBox *, GtkListBoxRow *row,
                                    gpointer data) {
    auto *self = static_cast<CatalogWindow *>(data);
    if (!row)
        return;
    const char *dep = static_cast<const char *>(g_object_get_data(G_OBJECT(row), "dep"));
    if (!dep)
        return;
    self->open_browse(dep_name(dep), "");
}

CatalogWindow::CatalogWindow(GtkApplication *app) : app_(app) {
    build_ui();
    populate_repos_page();
    gtk_window_set_default_size(GTK_WINDOW(window_), 1080, 680);
    refresh_installed();
    load_home();
    if (!has_repos())
        show_init_screen();
}

CatalogWindow::~CatalogWindow() {
    if (browse_timeout_)
        g_source_remove(browse_timeout_);
    if (home_carousel_timeout_)
        g_source_remove(home_carousel_timeout_);
}

void CatalogWindow::apply_icon_style() {
    GdkDisplay *display = gtk_widget_get_display(window_);
    GtkIconTheme *theme = gtk_icon_theme_get_for_display(display);
    static gboolean resource_theme_added = FALSE;
    if (!resource_theme_added) {
        gtk_icon_theme_add_resource_path(theme, "/org/catalog/icons");
        resource_theme_added = TRUE;
    }
    if (const char *name = icon_theme_name(icon_style_))
        g_object_set(gtk_settings_get_for_display(display),
                     "gtk-icon-theme-name", name, NULL);
}

void CatalogWindow::build_ui() {
    icon_style_ = read_icon_style();
    window_ = adw_application_window_new(app_);
    gtk_window_set_title(GTK_WINDOW(window_), "Catalog");
    gtk_window_set_icon_name(GTK_WINDOW(window_), "catalog");
    apply_icon_style();

    toast_overlay_ = adw_toast_overlay_new();

    GtkWidget *toolbar = adw_toolbar_view_new();

    GtkWidget *header = adw_header_bar_new();
    title_ = adw_window_title_new("Catalog", "univ store");
    adw_header_bar_set_title_widget(ADW_HEADER_BAR(header), title_);

    view_switcher_ = adw_view_switcher_new();
    adw_header_bar_pack_start(ADW_HEADER_BAR(header), view_switcher_);

    spinner_ = gtk_spinner_new();
    gtk_widget_set_visible(spinner_, FALSE);
    adw_header_bar_pack_end(ADW_HEADER_BAR(header), spinner_);

    refresh_btn_ = gtk_button_new_from_icon_name("view-refresh-symbolic");
    gtk_widget_set_tooltip_text(refresh_btn_, "Refresh installed packages");
    g_signal_connect(refresh_btn_, "clicked", G_CALLBACK(on_refresh_clicked),
                     this);
    adw_header_bar_pack_end(ADW_HEADER_BAR(header), refresh_btn_);

    update_btn_ = gtk_button_new_from_icon_name(
        "software-update-available-symbolic");
    gtk_widget_set_tooltip_text(update_btn_, "Upgrade all packages");
    g_signal_connect(update_btn_, "clicked", G_CALLBACK(on_update_clicked),
                     this);
    adw_header_bar_pack_end(ADW_HEADER_BAR(header), update_btn_);

    GtkWidget *style_btn = gtk_menu_button_new();
    gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(style_btn),
                                  "preferences-system-symbolic");
    gtk_widget_set_tooltip_text(style_btn, "Icon style");
    gtk_widget_add_css_class(style_btn, "flat");
    adw_header_bar_pack_end(ADW_HEADER_BAR(header), style_btn);

    GtkWidget *style_pop = gtk_popover_new();
    GtkWidget *style_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_margin_start(style_box, 8);
    gtk_widget_set_margin_end(style_box, 8);
    gtk_widget_set_margin_top(style_box, 8);
    gtk_widget_set_margin_bottom(style_box, 8);

    GtkWidget *style_label = gtk_label_new("Icon style");
    gtk_widget_add_css_class(style_label, "heading");
    gtk_label_set_xalign(GTK_LABEL(style_label), 0.0f);
    gtk_widget_set_margin_bottom(style_label, 4);
    gtk_box_append(GTK_BOX(style_box), style_label);

    static const char *style_labels[] = {"GNOME (Adwaita)", "KDE (Breeze)",
                                         "Follow system"};
    GtkWidget *group = nullptr;
    for (int i = 0; i < 3; i++) {
        GtkWidget *radio = gtk_check_button_new_with_label(style_labels[i]);
        if (group)
            gtk_check_button_set_group(GTK_CHECK_BUTTON(radio),
                                       GTK_CHECK_BUTTON(group));
        else
            group = radio;
        if (i == static_cast<int>(icon_style_))
            gtk_check_button_set_active(GTK_CHECK_BUTTON(radio), TRUE);
        g_object_set_data(G_OBJECT(radio), "style", GINT_TO_POINTER(i));
        g_signal_connect(radio, "toggled", G_CALLBACK(on_icon_style_toggled),
                         this);
        gtk_box_append(GTK_BOX(style_box), radio);
    }

    gtk_popover_set_child(GTK_POPOVER(style_pop), style_box);
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(style_btn), style_pop);

    stack_ = GTK_WIDGET(adw_view_stack_new());
    adw_view_switcher_set_stack(ADW_VIEW_SWITCHER(view_switcher_),
                                ADW_VIEW_STACK(stack_));

    adw_view_stack_add_titled_with_icon(ADW_VIEW_STACK(stack_),
                                        build_home_page(), "home", "Home",
                                        "go-home-symbolic");
    adw_view_stack_add_titled_with_icon(
        ADW_VIEW_STACK(stack_), build_repos_page(), "repos", "Repositories",
        "folder-download-symbolic");
    adw_view_stack_add_titled_with_icon(
        ADW_VIEW_STACK(stack_), build_list_page(false), "installed",
        "Installed", "system-software-install-symbolic");
    adw_view_stack_add_titled_with_icon(ADW_VIEW_STACK(stack_),
                                        build_list_page(true), "browse",
                                        "Browse", "edit-find-symbolic");
    adw_view_stack_add_titled_with_icon(
        ADW_VIEW_STACK(stack_), build_log_page(), "log", "Log",
        "view-list-symbolic");

    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(toolbar), header);
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(toolbar), stack_);

    GtkWidget *overlay = gtk_overlay_new();
    gtk_overlay_set_child(GTK_OVERLAY(overlay), toolbar);
    adw_toast_overlay_set_child(ADW_TOAST_OVERLAY(toast_overlay_), overlay);

    build_init_screen();
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), init_screen_);

    adw_application_window_set_content(ADW_APPLICATION_WINDOW(window_),
                                       toast_overlay_);

    GtkEventController *keys = gtk_event_controller_key_new();
    g_signal_connect(keys, "key-pressed", G_CALLBACK(on_key_pressed), this);
    gtk_widget_add_controller(window_, keys);

    log("catalog started");
    log("univ: " + univ_bin());
}

GtkWidget *CatalogWindow::build_list_page(bool browse) {
    GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);

    GtkWidget *left = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    GtkWidget *search = gtk_search_entry_new();
    g_object_set(G_OBJECT(search), "placeholder-text",
                 browse ? "Search the repositories…" : "Filter installed…",
                 NULL);
    gtk_widget_set_margin_start(search, 12);
    gtk_widget_set_margin_end(search, 12);
    gtk_widget_set_margin_top(search, 12);
    gtk_widget_set_margin_bottom(search, 6);

    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_has_frame(GTK_SCROLLED_WINDOW(scroll), FALSE);

    GtkWidget *list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_SINGLE);
    gtk_widget_set_hexpand(list, TRUE);
    gtk_widget_set_vexpand(list, TRUE);

    GtkWidget *placeholder = gtk_label_new(
        browse ? "Search the repositories…" : "Loading installed packages…");
    gtk_widget_add_css_class(placeholder, "dim-label");
    gtk_widget_set_margin_top(placeholder, 32);
    gtk_list_box_set_placeholder(GTK_LIST_BOX(list), placeholder);

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), list);
    gtk_box_append(GTK_BOX(left), search);
    gtk_box_append(GTK_BOX(left), scroll);

    DetailPane &detail = browse ? browse_detail_ : installed_detail_;
    detail.build();

    if (browse) {
        browse_list_ = list;
        browse_placeholder_ = placeholder;
        browse_search_ = search;
        g_signal_connect(list, "row-selected", G_CALLBACK(on_browse_selected),
                         this);
        g_signal_connect(search, "search-changed",
                         G_CALLBACK(on_browse_search_changed), this);
        g_signal_connect(search, "activate",
                         G_CALLBACK(on_browse_search_activate), this);
        g_signal_connect(detail.install_btn, "clicked",
                         G_CALLBACK(on_browse_install_clicked), this);
        g_signal_connect(detail.uninstall_btn, "clicked",
                         G_CALLBACK(on_browse_uninstall_clicked), this);
    } else {
        installed_list_ = list;
        installed_placeholder_ = placeholder;
        installed_search_ = search;
        g_signal_connect(list, "row-selected",
                         G_CALLBACK(on_installed_selected), this);
        g_signal_connect(search, "search-changed",
                         G_CALLBACK(on_installed_filter_changed), this);
        g_signal_connect(detail.install_btn, "clicked",
                         G_CALLBACK(on_installed_install_clicked), this);
        g_signal_connect(detail.uninstall_btn, "clicked",
                         G_CALLBACK(on_installed_uninstall_clicked), this);
    }

    gtk_paned_set_start_child(GTK_PANED(paned), left);
    gtk_paned_set_end_child(GTK_PANED(paned), detail.paned);
    gtk_widget_set_size_request(left, 380, -1);
    gtk_paned_set_position(GTK_PANED(paned), 400);
    gtk_paned_set_wide_handle(GTK_PANED(paned), FALSE);

    g_signal_connect(detail.deps_list, "row-selected",
                     G_CALLBACK(on_dep_selected), this);

    return paned;
}

GtkWidget *CatalogWindow::build_log_page() {
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    log_view_ = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(log_view_), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(log_view_), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(log_view_), TRUE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(log_view_), GTK_WRAP_NONE);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(log_view_), 8);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(log_view_), 8);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(log_view_), 8);
    gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(log_view_), 8);
    log_buffer_ = gtk_text_view_get_buffer(GTK_TEXT_VIEW(log_view_));
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), log_view_);
    return scroll;
}

GtkWidget *CatalogWindow::build_home_page() {
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_has_frame(GTK_SCROLLED_WINDOW(scroll), FALSE);

    home_featured_ = adw_carousel_new();
    gtk_widget_set_size_request(home_featured_, -1, 300);
    gtk_widget_set_margin_start(home_featured_, 12);
    gtk_widget_set_margin_end(home_featured_, 12);
    adw_carousel_set_allow_long_swipes(ADW_CAROUSEL(home_featured_), FALSE);
    adw_carousel_set_allow_scroll_wheel(ADW_CAROUSEL(home_featured_), FALSE);

    home_featured_dots_ = adw_carousel_indicator_dots_new();
    adw_carousel_indicator_dots_set_carousel(
        ADW_CAROUSEL_INDICATOR_DOTS(home_featured_dots_),
        ADW_CAROUSEL(home_featured_));
    gtk_widget_set_margin_top(home_featured_dots_, 4);

    GtkWidget *top = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(top, 32);
    gtk_widget_set_margin_bottom(top, 32);
    gtk_box_append(GTK_BOX(top), home_featured_);
    gtk_box_append(GTK_BOX(top), home_featured_dots_);

    GtkWidget *clamp = adw_clamp_new();
    adw_clamp_set_maximum_size(ADW_CLAMP(clamp), 1080);
    gtk_widget_set_margin_start(clamp, 24);
    gtk_widget_set_margin_end(clamp, 24);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);

    GtkWidget *welcome = gtk_label_new("Welcome to Catalog");
    gtk_widget_add_css_class(welcome, "title-1");
    gtk_label_set_xalign(GTK_LABEL(welcome), 0.0f);
    gtk_widget_set_margin_bottom(welcome, 4);

    GtkWidget *tagline = gtk_label_new(
        "Browse and install packages from your repositories.");
    gtk_widget_add_css_class(tagline, "dim-label");
    gtk_label_set_xalign(GTK_LABEL(tagline), 0.0f);
    gtk_widget_set_margin_bottom(tagline, 8);

    GtkWidget *repos_heading = gtk_label_new("Repositories");
    gtk_widget_add_css_class(repos_heading, "heading");
    gtk_label_set_xalign(GTK_LABEL(repos_heading), 0.0f);
    gtk_widget_set_margin_top(repos_heading, 12);

    home_repos_ = gtk_flow_box_new();
    gtk_widget_set_halign(home_repos_, GTK_ALIGN_CENTER);
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(home_repos_),
                                    GTK_SELECTION_NONE);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(home_repos_), 6);
    gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(home_repos_), 1);
    gtk_flow_box_set_homogeneous(GTK_FLOW_BOX(home_repos_), FALSE);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(home_repos_), 8);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(home_repos_), 8);

    gtk_box_append(GTK_BOX(box), welcome);
    gtk_box_append(GTK_BOX(box), tagline);
    gtk_box_append(GTK_BOX(box), repos_heading);
    gtk_box_append(GTK_BOX(box), home_repos_);

    adw_clamp_set_child(ADW_CLAMP(clamp), box);
    gtk_box_append(GTK_BOX(top), clamp);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), top);

    populate_repos();
    return scroll;
}

GtkWidget *CatalogWindow::build_repos_page() {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_has_frame(GTK_SCROLLED_WINDOW(scroll), FALSE);

    GtkWidget *list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_NONE);
    gtk_widget_set_hexpand(list, TRUE);
    gtk_widget_set_vexpand(list, TRUE);

    GtkWidget *placeholder = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    GtkWidget *placeholder_label =
        gtk_label_new("No repositories configured yet");
    gtk_widget_add_css_class(placeholder_label, "dim-label");
    gtk_widget_set_margin_top(placeholder_label, 24);
    gtk_box_append(GTK_BOX(placeholder), placeholder_label);

    GtkWidget *add_card = make_repo_button("Add repository",
                                           "configure a new source", "");
    g_object_set_data(G_OBJECT(add_card), "window", this);
    g_signal_connect(add_card, "clicked", G_CALLBACK(on_add_repo_clicked),
                     nullptr);
    gtk_box_append(GTK_BOX(placeholder), add_card);

    gtk_list_box_set_placeholder(GTK_LIST_BOX(list), placeholder);

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), list);
    gtk_box_append(GTK_BOX(box), scroll);

    repos_list_ = list;
    repos_placeholder_ = placeholder;
    return box;
}

static void ensure_init_styles(GtkWidget *anchor) {
    static gboolean added = FALSE;
    if (added)
        return;
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_string(
        provider,
        ".catalog-init-dim { background-color: rgba(0, 0, 0, 0.35); }\n"
        ".catalog-init-btn {\n"
        "  background-color: #62a0ea;\n"
        "  color: #ffffff;\n"
        "  border-radius: 8px;\n"
        "  padding: 10px 28px;\n"
        "  font-weight: 600;\n"
        "}\n"
        ".catalog-init-btn:hover { background-color: #73adf1; }\n"
        ".catalog-init-btn:disabled { background-color: #9db9de; }\n");
    gtk_style_context_add_provider_for_display(
        gtk_widget_get_display(anchor), GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    added = TRUE;
}

void CatalogWindow::build_init_screen() {
    ensure_init_styles(window_);

    GtkWidget *dim = gtk_overlay_new();
    gtk_widget_set_hexpand(dim, TRUE);
    gtk_widget_set_vexpand(dim, TRUE);
    gtk_widget_set_halign(dim, GTK_ALIGN_FILL);
    gtk_widget_set_valign(dim, GTK_ALIGN_FILL);
    gtk_widget_add_css_class(dim, "catalog-init-dim");

    GtkWidget *catcher = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_hexpand(catcher, TRUE);
    gtk_widget_set_vexpand(catcher, TRUE);
    gtk_widget_set_halign(catcher, GTK_ALIGN_FILL);
    gtk_widget_set_valign(catcher, GTK_ALIGN_FILL);
    GtkGesture *click = gtk_gesture_click_new();
    g_signal_connect(click, "pressed",
                     G_CALLBACK(+[](GtkGesture *, int, double, gpointer) {}),
                     nullptr);
    gtk_widget_add_controller(catcher, GTK_EVENT_CONTROLLER(click));
    gtk_overlay_set_child(GTK_OVERLAY(dim), catcher);

    GtkWidget *center = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_halign(center, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(center, GTK_ALIGN_CENTER);

    GtkWidget *title = gtk_label_new("No repositories configured");
    gtk_widget_add_css_class(title, "title-1");

    GtkWidget *subtitle = gtk_label_new(
        "Initialize univ to create the store and add the default deb & rpm "
        "repositories.");
    gtk_widget_add_css_class(subtitle, "body");
    gtk_widget_add_css_class(subtitle, "dim-label");
    gtk_label_set_wrap(GTK_LABEL(subtitle), TRUE);
    gtk_label_set_justify(GTK_LABEL(subtitle), GTK_JUSTIFY_CENTER);

    init_btn_ = gtk_button_new_with_label("Initialize");
    gtk_widget_add_css_class(init_btn_, "catalog-init-btn");
    g_signal_connect(init_btn_, "clicked", G_CALLBACK(on_init_clicked), this);

    gtk_box_append(GTK_BOX(center), title);
    gtk_box_append(GTK_BOX(center), subtitle);
    gtk_box_append(GTK_BOX(center), init_btn_);
    gtk_overlay_add_overlay(GTK_OVERLAY(dim), center);

    gtk_widget_set_visible(dim, FALSE);
    init_screen_ = dim;
}

void CatalogWindow::show_init_screen() {
    if (init_screen_)
        gtk_widget_set_visible(init_screen_, TRUE);
}

void CatalogWindow::hide_init_screen() {
    if (init_screen_)
        gtk_widget_set_visible(init_screen_, FALSE);
}

bool CatalogWindow::has_repos() const {
    std::string base = std::string(g_get_home_dir()) + "/.local/univ/";
    std::string paths[] = {base + "debrepos.conf", base + "rpmrepos.conf"};
    for (const std::string &path : paths) {
        g_autofree char *contents = nullptr;
        if (!g_file_get_contents(path.c_str(), &contents, nullptr, nullptr))
            continue;
        char **lines = g_strsplit(contents, "\n", -1);
        for (char **line = lines; *line; line++) {
            char *trimmed = g_strstrip(*line);
            if (*trimmed == '\0' || *trimmed == '#')
                continue;
            g_strfreev(lines);
            return true;
        }
        g_strfreev(lines);
    }
    return false;
}

void CatalogWindow::on_init_clicked(GtkButton *, gpointer data) {
    auto *self = static_cast<CatalogWindow *>(data);
    if (self->busy())
        return;
    gtk_widget_set_sensitive(self->init_btn_, FALSE);
    self->start_task(
        "initializing univ", {"init"}, [self](int code) {
            gtk_widget_set_sensitive(self->init_btn_, TRUE);
            if (code == 0 && self->has_repos()) {
                self->populate_repos_page();
                self->populate_repos();
                self->load_home();
                self->refresh_installed();
                self->hide_init_screen();
            }
        });
}

void CatalogWindow::populate_repos_page() {
    clear_list(GTK_LIST_BOX(repos_list_));

    std::vector<std::pair<std::string, std::string>> repos;
    std::string base = std::string(g_get_home_dir()) + "/.local/univ/";
    append_repos_from_file(base + "debrepos.conf", "deb", repos);
    append_repos_from_file(base + "rpmrepos.conf", "rpm", repos);

    bool any = false;
    for (size_t i = 0; i < repos.size(); i++) {
        bool seen = false;
        for (size_t j = 0; j < i; j++)
            if (repos[j].first == repos[i].first) {
                seen = true;
                break;
            }
        if (seen)
            continue;
        any = true;

        GtkWidget *row = gtk_list_box_row_new();
        GtkWidget *action = adw_action_row_new();
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(action),
                                      repos[i].first.c_str());
        adw_action_row_set_subtitle(
            ADW_ACTION_ROW(action),
            repos[i].second == "deb" ? "deb repository" : "rpm repository");
        GtkWidget *remove = gtk_button_new_from_icon_name(
            "edit-delete-symbolic");
        gtk_widget_set_tooltip_text(remove, "Remove repository");
        gtk_widget_add_css_class(remove, "flat");
        g_object_set_data(G_OBJECT(remove), "window", this);
        g_object_set_data_full(G_OBJECT(remove), "name",
                               g_strdup(repos[i].first.c_str()), g_free);
        g_object_set_data_full(G_OBJECT(remove), "kind",
                               g_strdup(repos[i].second.c_str()), g_free);
        g_signal_connect(remove, "clicked",
                         G_CALLBACK(on_remove_repo_clicked), nullptr);
        adw_action_row_add_suffix(ADW_ACTION_ROW(action), remove);
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), action);
        gtk_list_box_append(GTK_LIST_BOX(repos_list_), row);
    }

    GtkWidget *add = make_repo_button("Add repository",
                                      "configure a new source", "");
    g_object_set_data(G_OBJECT(add), "window", this);
    g_signal_connect(add, "clicked", G_CALLBACK(on_add_repo_clicked), nullptr);
    if (any)
        gtk_list_box_append(GTK_LIST_BOX(repos_list_), add);
}

void CatalogWindow::remove_repo(const std::string &name,
                                const std::string &kind) {
    std::string base = std::string(g_get_home_dir()) + "/.local/univ/";
    std::string path =
        base + (kind == "deb" ? "debrepos.conf" : "rpmrepos.conf");
    remove_repo_from_file(path, name);
    populate_repos_page();
    populate_repos();
    toast("Removed " + name);
}

void CatalogWindow::on_remove_repo_clicked(GtkButton *button, gpointer) {
    auto *self = static_cast<CatalogWindow *>(
        g_object_get_data(G_OBJECT(button), "window"));
    const char *name = static_cast<const char *>(
        g_object_get_data(G_OBJECT(button), "name"));
    const char *kind = static_cast<const char *>(
        g_object_get_data(G_OBJECT(button), "kind"));
    self->remove_repo(name, kind);
}

void CatalogWindow::load_home() {
    std::vector<Package> cached = read_featured_cache();
    if (!cached.empty()) {
        home_candidates_ = std::move(cached);
        populate_home();
    }
    query_packages({"search", "", "--json"},
                   [this](bool ok, std::vector<Package> pkgs,
                          const std::string &) {
                       if (ok && !pkgs.empty()) {
                           dedupe_packages(pkgs);
                           home_candidates_ = std::move(pkgs);
                           write_featured_cache(home_candidates_);
                       }
                       populate_home();
                   });
}

void CatalogWindow::populate_home() {
    if (!home_featured_)
        return;
    AdwCarousel *carousel = ADW_CAROUSEL(home_featured_);
    while (adw_carousel_get_n_pages(carousel) > 0)
        adw_carousel_remove(carousel,
                            adw_carousel_get_nth_page(carousel, 0));

    std::vector<Package> pool = home_candidates_;
    dedupe_packages(pool);
    shuffle_packages(pool);
    size_t n = std::min<size_t>(pool.size(), 5);
    std::vector<GtkWidget *> banners;
    banners.reserve(n);
    for (size_t i = 0; i < n; i++) {
        GtkWidget *banner = make_featured_banner(pool[i]);
        gtk_widget_add_css_class(
            banner, ("featured-banner-" + std::to_string(i)).c_str());
        g_signal_connect(banner, "clicked", G_CALLBACK(on_featured_clicked),
                         nullptr);
        g_object_set_data(G_OBJECT(banner), "window", this);
        g_object_set_data_full(G_OBJECT(banner), "package",
                               g_strdup(pool[i].name.c_str()), g_free);
        banners.push_back(banner);
        adw_carousel_append(carousel, banner);
    }
    set_featured_colors(banners);
    if (n > 1 && !home_carousel_timeout_)
        home_carousel_timeout_ =
            g_timeout_add(4000, on_featured_tick, this);
}

gboolean CatalogWindow::on_featured_tick(gpointer data) {
    auto *self = static_cast<CatalogWindow *>(data);
    AdwCarousel *carousel = ADW_CAROUSEL(self->home_featured_);
    guint n = adw_carousel_get_n_pages(carousel);
    if (n > 1) {
        int cur =
            static_cast<int>(std::llround(adw_carousel_get_position(carousel)));
        int next = (cur + 1) % static_cast<int>(n);
        adw_carousel_scroll_to(carousel,
                               adw_carousel_get_nth_page(carousel, next), TRUE);
    }
    return G_SOURCE_CONTINUE;
}

void CatalogWindow::populate_repos() {
    ensure_repo_styles(home_repos_);
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(home_repos_)) != nullptr)
        gtk_flow_box_remove(GTK_FLOW_BOX(home_repos_), child);

    auto add_button = [this](const std::string &title,
                             const std::string &subtitle,
                             const std::string &repo) {
        GtkWidget *btn = make_repo_button(title, subtitle, repo);
        g_signal_connect(btn, "clicked", G_CALLBACK(on_repo_clicked), nullptr);
        g_object_set_data(G_OBJECT(btn), "window", this);
        g_object_set_data_full(G_OBJECT(btn), "repo",
                               g_strdup(repo.c_str()), g_free);
        gtk_flow_box_append(GTK_FLOW_BOX(home_repos_), btn);
    };

    add_button("All repositories", "show everything", "");

    std::vector<std::pair<std::string, std::string>> repos;
    std::string base = std::string(g_get_home_dir()) + "/.local/univ/";
    append_repos_from_file(base + "debrepos.conf", "deb", repos);
    append_repos_from_file(base + "rpmrepos.conf", "rpm", repos);

    for (size_t i = 0; i < repos.size(); i++) {
        bool seen = false;
        for (size_t j = 0; j < i; j++)
            if (repos[j].first == repos[i].first) {
                seen = true;
                break;
            }
        if (seen)
            continue;
        std::string sub = repos[i].second == "deb" ? "deb repository"
                                                   : "rpm repository";
        add_button(repos[i].first, sub, repos[i].first);
    }
}

void CatalogWindow::open_browse(const std::string &query,
                                const std::string &repo) {
    browse_repo_ = repo;
    browse_query_ = query;
    if (browse_timeout_) {
        g_source_remove(browse_timeout_);
        browse_timeout_ = 0;
    }
    if (repo.empty())
        g_object_set(G_OBJECT(browse_search_), "placeholder-text",
                     "Search the repositories…", NULL);
    else
        g_object_set(G_OBJECT(browse_search_), "placeholder-text",
                     ("Search " + repo + "…").c_str(), NULL);
    gtk_editable_set_text(GTK_EDITABLE(browse_search_), query.c_str());
    if (browse_timeout_) {
        g_source_remove(browse_timeout_);
        browse_timeout_ = 0;
    }
    adw_view_stack_set_visible_child_name(ADW_VIEW_STACK(stack_), "browse");
    do_search();
}

void CatalogWindow::populate_installed() {
    clear_list(GTK_LIST_BOX(installed_list_));
    std::string q = lower(installed_filter_);
    bool any = false;
    for (size_t i = 0; i < installed_.size(); i++) {
        const Package &p = installed_[i];
        if (!q.empty() && lower(p.name).find(q) == std::string::npos &&
            lower(p.description).find(q) == std::string::npos)
            continue;
        any = true;
        GtkWidget *row = make_row(p);
        g_object_set_data(G_OBJECT(row), "index",
                          GINT_TO_POINTER(static_cast<int>(i)));
        gtk_list_box_append(GTK_LIST_BOX(installed_list_), row);
    }

    std::string message;
    if (!installed_error_.empty())
        message = "Could not list packages: " + installed_error_;
    else if (!installed_loaded_)
        message = "Loading installed packages…";
    else if (installed_no_repos_)
        message = "No repositories configured";
    else if (installed_.empty())
        message = "No packages installed";
    else if (!any)
        message = "No packages match \"" + installed_filter_ + "\"";
    gtk_label_set_text(GTK_LABEL(installed_placeholder_), message.c_str());
}

void CatalogWindow::refresh_installed() {
    installed_loaded_ = false;
    installed_error_.clear();
    installed_no_repos_ = false;
    if (!has_repos()) {
        installed_loaded_ = true;
        installed_.clear();
        installed_no_repos_ = true;
        populate_installed();
        update_buttons();
        return;
    }
    populate_installed();

    query_packages({"list", "--json"},
                   [this](bool ok, std::vector<Package> pkgs,
                          const std::string &message) {
                       installed_loaded_ = true;
                       installed_selected_ = -1;
                       installed_detail_.clear();
                       if (ok) {
                           installed_ = std::move(pkgs);
                           installed_error_.clear();
                       } else {
                           installed_.clear();
                           installed_error_ = message;
                           toast("Could not load packages: " + message);
                       }
                       populate_installed();
                       update_buttons();
                       std::string subtitle = ok
                                                  ? (std::to_string(
                                                         installed_.size()) +
                                                     " installed")
                                                  : "univ store";
                       adw_window_title_set_subtitle(
                           ADW_WINDOW_TITLE(title_), subtitle.c_str());
                   });
}

void CatalogWindow::set_browse_no_results(bool no_results) {
    if (no_results)
        gtk_widget_add_css_class(browse_search_, "catalog-noresults");
    else
        gtk_widget_remove_css_class(browse_search_, "catalog-noresults");
}

void CatalogWindow::do_search() {
    std::string q = browse_query_;
    if (q.empty() && browse_repo_.empty()) {
        browse_.clear();
        browse_selected_ = -1;
        clear_list(GTK_LIST_BOX(browse_list_));
        browse_detail_.clear();
        set_browse_no_results(false);
        gtk_label_set_text(GTK_LABEL(browse_placeholder_),
                           "Search the repositories…");
        return;
    }

    set_browse_no_results(false);
    std::string hint = browse_repo_.empty() ? "Searching…"
                                            : "Loading " + browse_repo_ + "…";
    gtk_label_set_text(GTK_LABEL(browse_placeholder_), hint.c_str());
    query_packages({"search", q, "--json"},
                   [this, q](bool ok, std::vector<Package> pkgs,
                             const std::string &message) {
                       if (browse_query_ != q)
                           return;
                       browse_selected_ = -1;
                       browse_detail_.clear();
                       if (ok) {
                           browse_.clear();
                           for (auto &p : pkgs)
                               if (browse_repo_.empty() ||
                                   p.repo == browse_repo_)
                                   browse_.push_back(std::move(p));
                       } else if (message.find("no packages match") !=
                                  std::string::npos) {
                           browse_.clear();
                       } else {
                           browse_.clear();
                           toast("Search failed: " + message);
                       }
                       populate_browse();
                       update_buttons();
                   });
}

void CatalogWindow::populate_browse() {
    clear_list(GTK_LIST_BOX(browse_list_));
    for (size_t i = 0; i < browse_.size(); i++) {
        GtkWidget *row = make_row(browse_[i]);
        g_object_set_data(G_OBJECT(row), "index",
                          GINT_TO_POINTER(static_cast<int>(i)));
        gtk_list_box_append(GTK_LIST_BOX(browse_list_), row);
    }

    std::string message;
    if (browse_.empty()) {
        if (!browse_repo_.empty())
            message = "No packages found in " + browse_repo_;
        else if (!browse_query_.empty())
            message = "Didn't find anything under \"" + browse_query_ + "\"";
        else
            message = "No packages found";
        set_browse_no_results(true);
    } else {
        message = std::to_string(browse_.size()) + " result(s)";
        set_browse_no_results(false);
    }
    gtk_label_set_text(GTK_LABEL(browse_placeholder_), message.c_str());
}

bool CatalogWindow::is_installed(const std::string &name) const {
    for (const auto &p : installed_)
        if (p.name == name)
            return true;
    return false;
}

void CatalogWindow::update_buttons() {
    bool ready = !busy();
    bool inst = installed_selected_ >= 0 &&
                installed_selected_ < static_cast<int>(installed_.size());
    bool brw = browse_selected_ >= 0 &&
               browse_selected_ < static_cast<int>(browse_.size());

    gtk_widget_set_sensitive(installed_detail_.install_btn, ready && inst);
    gtk_widget_set_sensitive(installed_detail_.uninstall_btn, ready && inst);
    gtk_widget_set_sensitive(browse_detail_.install_btn, ready && brw);
    gtk_widget_set_sensitive(browse_detail_.uninstall_btn,
                             ready && brw &&
                                 is_installed(browse_[browse_selected_].name));

    if (inst)
        gtk_button_set_label(GTK_BUTTON(installed_detail_.install_btn),
                             "Reinstall");
    if (brw) {
        bool already = is_installed(browse_[browse_selected_].name);
        gtk_button_set_label(GTK_BUTTON(browse_detail_.install_btn),
                             already ? "Reinstall" : "Install");
    }
}

void CatalogWindow::start_task(const std::string &title,
                               std::vector<std::string> args,
                               std::function<void(int)> on_done) {
    if (busy())
        return;
    log("> " + title);
    set_busy(true);
    stream_task(std::move(args),
                [this](const std::string &line) { log(line); },
                [this, title, on_done](int code) {
                    set_busy(false);
                    log("[exit " + std::to_string(code) + "] " + title);
                    if (code == 0)
                        toast(title + " finished");
                    else
                        toast(title + " failed (exit " + std::to_string(code) +
                              ")");
                    refresh_installed();
                    update_buttons();
                    if (on_done)
                        on_done(code);
                });
}

void CatalogWindow::set_busy(bool busy) {
    busy_count_ += busy ? 1 : -1;
    bool b = busy_count_ > 0;
    gtk_spinner_set_spinning(GTK_SPINNER(spinner_), b);
    gtk_widget_set_visible(spinner_, b);
    gtk_widget_set_sensitive(refresh_btn_, !b);
    gtk_widget_set_sensitive(update_btn_, !b);
    gtk_widget_set_sensitive(view_switcher_, !b);
    update_buttons();
}

void CatalogWindow::log(const std::string &line) {
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(log_buffer_, &end);
    gtk_text_buffer_insert(log_buffer_, &end, line.c_str(), -1);
    gtk_text_buffer_insert(log_buffer_, &end, "\n", -1);

    if (gtk_text_buffer_get_line_count(log_buffer_) > 2000) {
        GtkTextIter start, next;
        gtk_text_buffer_get_start_iter(log_buffer_, &start);
        next = start;
        for (int i = 0; i < 500 && gtk_text_iter_forward_line(&next); i++) {
        }
        gtk_text_buffer_delete(log_buffer_, &start, &next);
    }

    gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW(log_view_),
                                 gtk_text_buffer_get_insert(log_buffer_), 0.0,
                                 FALSE, 0.0, 0.0);
}

void CatalogWindow::toast(const std::string &message) {
    adw_toast_overlay_add_toast(ADW_TOAST_OVERLAY(toast_overlay_),
                                adw_toast_new(message.c_str()));
}

void CatalogWindow::installed_install() {
    if (busy() || installed_selected_ < 0 ||
        installed_selected_ >= static_cast<int>(installed_.size()))
        return;
    const Package &p = installed_[installed_selected_];
    start_task("reinstalling " + p.name, {"install", p.name});
}

void CatalogWindow::installed_uninstall() {
    if (busy() || installed_selected_ < 0 ||
        installed_selected_ >= static_cast<int>(installed_.size()))
        return;
    const Package &p = installed_[installed_selected_];
    start_task("uninstalling " + p.name, {"uninstall", p.name});
}

void CatalogWindow::browse_install() {
    if (busy() || browse_selected_ < 0 ||
        browse_selected_ >= static_cast<int>(browse_.size()))
        return;
    const Package &p = browse_[browse_selected_];
    if (p.kind == "rpm")
        start_task("installing " + p.name, {"install-rpm", p.name});
    else
        start_task("installing " + p.name, {"install", p.name});
}

void CatalogWindow::browse_uninstall() {
    if (busy() || browse_selected_ < 0 ||
        browse_selected_ >= static_cast<int>(browse_.size()))
        return;
    const Package &p = browse_[browse_selected_];
    start_task("uninstalling " + p.name, {"uninstall", p.name});
}

void CatalogWindow::upgrade_all() {
    if (busy())
        return;
    start_task("upgrading all packages", {"upgrade"});
}
}
