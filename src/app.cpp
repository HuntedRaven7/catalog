#include "app.h"

#include "univ.h"

#include <adw-compat.h>
#include <adapta.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
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
}

CatalogWindow::~CatalogWindow() {
    if (browse_timeout_)
        g_source_remove(browse_timeout_);
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
    if (q.empty()) {
        browse_.clear();
        browse_selected_ = -1;
        clear_list(GTK_LIST_BOX(browse_list_));
        browse_detail_.clear();
        gtk_label_set_text(GTK_LABEL(browse_placeholder_),
                           "Search the repositories…");
        return;
    }

    gtk_label_set_text(GTK_LABEL(browse_placeholder_), "Searching…");
    query_packages({"search", q, "--json"},
                   [this, q](bool ok, std::vector<Package> pkgs,
                             const std::string &message) {
                       if (browse_query_ != q)
                           return;
                       browse_selected_ = -1;
                       browse_detail_.clear();
                       if (ok) {
                           browse_ = std::move(pkgs);
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
        message = "No packages found";
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
