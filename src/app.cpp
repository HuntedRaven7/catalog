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
        ".repo-sub { color: rgba(255, 255, 255, 0.8); }\n");
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

    depends = gtk_label_new("");
    gtk_widget_add_css_class(depends, "dim-label");
    gtk_label_set_wrap(GTK_LABEL(depends), TRUE);
    gtk_label_set_selectable(GTK_LABEL(depends), TRUE);
    gtk_label_set_xalign(GTK_LABEL(depends), 0.0f);

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
    gtk_box_append(GTK_BOX(box), depends);
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
    if (p.depends.empty()) {
        gtk_widget_set_visible(depends, FALSE);
    } else {
        gtk_widget_set_visible(depends, TRUE);
        gtk_label_set_text(GTK_LABEL(depends),
                           ("Depends: " + p.depends).c_str());
    }
}

void CatalogWindow::DetailPane::clear() {
    gtk_stack_set_visible_child_name(GTK_STACK(stack), "empty");
    gtk_widget_set_sensitive(install_btn, FALSE);
    gtk_widget_set_sensitive(uninstall_btn, FALSE);
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

void CatalogWindow::on_featured_clicked(GtkButton *button, gpointer data) {
    auto *self = static_cast<CatalogWindow *>(
        g_object_get_data(G_OBJECT(button), "window"));
    std::string name(static_cast<const char *>(data));
    g_free(data);
    self->open_browse(name, "");
}

void CatalogWindow::on_repo_clicked(GtkButton *button, gpointer data) {
    auto *self = static_cast<CatalogWindow *>(
        g_object_get_data(G_OBJECT(button), "window"));
    std::string repo(static_cast<const char *>(data));
    g_free(data);
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

CatalogWindow::CatalogWindow(GtkApplication *app) : app_(app) {
    build_ui();
    gtk_window_set_default_size(GTK_WINDOW(window_), 1080, 680);
    refresh_installed();
    load_home();
}

CatalogWindow::~CatalogWindow() {
    if (browse_timeout_)
        g_source_remove(browse_timeout_);
    if (home_carousel_timeout_)
        g_source_remove(home_carousel_timeout_);
}

void CatalogWindow::build_ui() {
    window_ = adw_application_window_new(app_);
    gtk_window_set_title(GTK_WINDOW(window_), "Catalog");
    gtk_window_set_icon_name(GTK_WINDOW(window_),
                             "package-x-generic-symbolic");

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

    stack_ = GTK_WIDGET(adw_view_stack_new());
    adw_view_switcher_set_stack(ADW_VIEW_SWITCHER(view_switcher_),
                                ADW_VIEW_STACK(stack_));

    adw_view_stack_add_titled_with_icon(ADW_VIEW_STACK(stack_),
                                        build_home_page(), "home", "Home",
                                        "go-home-symbolic");
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
    adw_toast_overlay_set_child(ADW_TOAST_OVERLAY(toast_overlay_), toolbar);

    adw_application_window_set_content(ADW_APPLICATION_WINDOW(window_),
                                       toast_overlay_);

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
    gtk_paned_set_end_child(GTK_PANED(paned), detail.stack);
    gtk_widget_set_size_request(left, 380, -1);
    gtk_paned_set_position(GTK_PANED(paned), 400);
    gtk_paned_set_wide_handle(GTK_PANED(paned), FALSE);

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
                         g_strdup(pool[i].name.c_str()));
        g_object_set_data(G_OBJECT(banner), "window", this);
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
        g_signal_connect(btn, "clicked", G_CALLBACK(on_repo_clicked),
                         g_strdup(repo.c_str()));
        g_object_set_data(G_OBJECT(btn), "window", this);
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

    GtkWidget *add = gtk_button_new();
    gtk_widget_add_css_class(add, "card");
    gtk_widget_set_size_request(add, 210, -1);
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_margin_start(row, 12);
    gtk_widget_set_margin_end(row, 12);
    gtk_widget_set_margin_top(row, 10);
    gtk_widget_set_margin_bottom(row, 10);
    GtkWidget *icon = gtk_image_new_from_icon_name("list-add-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(icon), 24);
    GtkWidget *label = gtk_label_new("Add repository");
    gtk_box_append(GTK_BOX(row), icon);
    gtk_box_append(GTK_BOX(row), label);
    gtk_button_set_child(GTK_BUTTON(add), row);
    g_signal_connect(add, "clicked", G_CALLBACK(on_add_repo_clicked), this);
    g_object_set_data(G_OBJECT(add), "window", this);
    gtk_flow_box_append(GTK_FLOW_BOX(home_repos_), add);
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
    else if (installed_.empty())
        message = "No packages installed";
    else if (!any)
        message = "No packages match \"" + installed_filter_ + "\"";
    gtk_label_set_text(GTK_LABEL(installed_placeholder_), message.c_str());
}

void CatalogWindow::refresh_installed() {
    installed_loaded_ = false;
    installed_error_.clear();
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

void CatalogWindow::do_search() {
    std::string q = browse_query_;
    if (q.empty() && browse_repo_.empty()) {
        browse_.clear();
        browse_selected_ = -1;
        clear_list(GTK_LIST_BOX(browse_list_));
        browse_detail_.clear();
        gtk_label_set_text(GTK_LABEL(browse_placeholder_),
                           "Search the repositories…");
        return;
    }

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
    if (browse_.empty())
        message = browse_repo_.empty()
                      ? "No packages found"
                      : "No packages found in " + browse_repo_;
    else
        message = std::to_string(browse_.size()) + " result(s)";
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
                               std::vector<std::string> args) {
    if (busy())
        return;
    log("> " + title);
    set_busy(true);
    stream_task(std::move(args),
                [this](const std::string &line) { log(line); },
                [this, title](int code) {
                    set_busy(false);
                    log("[exit " + std::to_string(code) + "] " + title);
                    if (code == 0)
                        toast(title + " finished");
                    else
                        toast(title + " failed (exit " + std::to_string(code) +
                              ")");
                    refresh_installed();
                    update_buttons();
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
