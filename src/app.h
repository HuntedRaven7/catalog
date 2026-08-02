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
        GtkWidget *paned = nullptr;
        GtkWidget *deps_list = nullptr;
        GtkWidget *deps_empty = nullptr;
        GtkWidget *name = nullptr;
        GtkWidget *version = nullptr;
        GtkWidget *meta = nullptr;
        GtkWidget *description = nullptr;
        GtkWidget *install_btn = nullptr;
        GtkWidget *uninstall_btn = nullptr;

        void build();
        void show_package(const Package &package);
        void show_deps(const std::string &depends);
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
    bool installed_no_repos_ = false;
    int installed_selected_ = -1;

    GtkWidget *browse_list_ = nullptr;
    GtkWidget *browse_placeholder_ = nullptr;
    GtkWidget *browse_search_ = nullptr;
    DetailPane browse_detail_;
    std::vector<Package> browse_;
    std::string browse_query_;
    std::string browse_repo_;
    guint browse_timeout_ = 0;
    int browse_selected_ = -1;

    GtkWidget *home_featured_ = nullptr;
    GtkWidget *home_featured_dots_ = nullptr;
    GtkWidget *home_repos_ = nullptr;
    std::vector<Package> home_candidates_;
    guint home_carousel_timeout_ = 0;

    GtkWidget *init_screen_ = nullptr;
    GtkWidget *init_btn_ = nullptr;

    GtkWidget *repos_list_ = nullptr;
    GtkWidget *repos_placeholder_ = nullptr;

    GtkWidget *log_view_ = nullptr;
    GtkTextBuffer *log_buffer_ = nullptr;

    int busy_count_ = 0;

    void build_ui();
    GtkWidget *build_list_page(bool browse);
    GtkWidget *build_log_page();
    GtkWidget *build_home_page();
    GtkWidget *build_repos_page();

    void build_init_screen();
    void show_init_screen();
    void hide_init_screen();
    bool has_repos() const;

    void populate_installed();
    void refresh_installed();
    void do_search();
    void populate_browse();
    void load_home();
    void populate_home();
    void populate_repos();
    void populate_repos_page();
    void remove_repo(const std::string &name, const std::string &kind);
    void set_browse_no_results(bool no_results);
    void open_browse(const std::string &query, const std::string &repo);
    void show_add_repo_dialog();

    bool is_installed(const std::string &name) const;
    void update_buttons();

    void start_task(const std::string &title, std::vector<std::string> args,
                    std::function<void(int)> on_done = nullptr);
    void set_busy(bool busy);
    void log(const std::string &line);
    void toast(const std::string &message);

    void installed_install();
    void installed_uninstall();
    void browse_install();
    void browse_uninstall();
    void upgrade_all();

    static std::string dep_name(const std::string &item);

    static void on_installed_selected(GtkListBox *, GtkListBoxRow *, gpointer);
    static void on_browse_selected(GtkListBox *, GtkListBoxRow *, gpointer);
    static void on_installed_filter_changed(GtkSearchEntry *, gpointer);
    static void on_browse_search_changed(GtkSearchEntry *, gpointer);
    static void on_browse_search_activate(GtkSearchEntry *, gpointer);
    static void on_featured_clicked(GtkButton *, gpointer);
    static void on_repo_clicked(GtkButton *, gpointer);
    static void on_add_repo_clicked(GtkButton *, gpointer);
    static void on_add_repo_submit(GtkButton *, gpointer);
    static void on_remove_repo_clicked(GtkButton *, gpointer);
    static gboolean on_featured_tick(gpointer);
    static void on_init_clicked(GtkButton *, gpointer);
    static void on_refresh_clicked(GtkButton *, gpointer);
    static void on_update_clicked(GtkButton *, gpointer);
    static void on_installed_install_clicked(GtkButton *, gpointer);
    static void on_installed_uninstall_clicked(GtkButton *, gpointer);
    static void on_browse_install_clicked(GtkButton *, gpointer);
    static void on_browse_uninstall_clicked(GtkButton *, gpointer);
    static void on_dep_selected(GtkListBox *, GtkListBoxRow *, gpointer);
    static gboolean on_key_pressed(GtkEventControllerKey *, guint, guint,
                                   GdkModifierType, gpointer);
};

}
