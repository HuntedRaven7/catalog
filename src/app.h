#pragma once

#include <gtk/gtk.h>

#include <functional>
#include <string>
#include <vector>

#include "model.h"

namespace catalog {

class CatalogWindow {
public:
    explicit CatalogWindow(GtkApplication *app);
    ~CatalogWindow();

    CatalogWindow(const CatalogWindow &) = delete;
    CatalogWindow &operator=(const CatalogWindow &) = delete;

    GtkWidget *widget() const { return window_; }

private:
    struct DetailPane {
        GtkWidget *stack = nullptr;
        GtkWidget *name = nullptr;
        GtkWidget *version = nullptr;
        GtkWidget *meta = nullptr;
        GtkWidget *description = nullptr;
        GtkWidget *depends = nullptr;
        GtkWidget *install_btn = nullptr;
        GtkWidget *uninstall_btn = nullptr;

        void build();
        void show_package(const Package &package);
        void clear();
    };

    GtkWidget *window_ = nullptr;
    GtkWidget *toast_overlay_ = nullptr;
    GtkWidget *title_ = nullptr;
    GtkWidget *spinner_ = nullptr;
    GtkWidget *refresh_btn_ = nullptr;
    GtkWidget *update_btn_ = nullptr;
    GtkWidget *view_switcher_ = nullptr;
    GtkWidget *stack_ = nullptr;
    GtkApplication *app_ = nullptr;

    bool busy() const { return busy_count_ > 0; }

    GtkWidget *installed_list_ = nullptr;
    GtkWidget *installed_placeholder_ = nullptr;
    GtkWidget *installed_search_ = nullptr;
    DetailPane installed_detail_;
    std::vector<Package> installed_;
    std::string installed_filter_;
    std::string installed_error_;
    bool installed_loaded_ = false;
    int installed_selected_ = -1;

    GtkWidget *browse_list_ = nullptr;
    GtkWidget *browse_placeholder_ = nullptr;
    GtkWidget *browse_search_ = nullptr;
    DetailPane browse_detail_;
    std::vector<Package> browse_;
    std::string browse_query_;
    guint browse_timeout_ = 0;
    int browse_selected_ = -1;

    GtkWidget *log_view_ = nullptr;
    GtkTextBuffer *log_buffer_ = nullptr;

    int busy_count_ = 0;

    void build_ui();
    GtkWidget *build_list_page(bool browse);
    GtkWidget *build_log_page();

    void populate_installed();
    void refresh_installed();
    void do_search();
    void populate_browse();

    bool is_installed(const std::string &name) const;
    void update_buttons();

    void start_task(const std::string &title, std::vector<std::string> args);
    void set_busy(bool busy);
    void log(const std::string &line);
    void toast(const std::string &message);

    void installed_install();
    void installed_uninstall();
    void browse_install();
    void browse_uninstall();
    void upgrade_all();

    static void on_installed_selected(GtkListBox *, GtkListBoxRow *, gpointer);
    static void on_browse_selected(GtkListBox *, GtkListBoxRow *, gpointer);
    static void on_installed_filter_changed(GtkSearchEntry *, gpointer);
    static void on_browse_search_changed(GtkSearchEntry *, gpointer);
    static void on_browse_search_activate(GtkSearchEntry *, gpointer);
    static void on_refresh_clicked(GtkButton *, gpointer);
    static void on_update_clicked(GtkButton *, gpointer);
    static void on_installed_install_clicked(GtkButton *, gpointer);
    static void on_installed_uninstall_clicked(GtkButton *, gpointer);
    static void on_browse_install_clicked(GtkButton *, gpointer);
    static void on_browse_uninstall_clicked(GtkButton *, gpointer);
};

}
