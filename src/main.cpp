#include <adw-compat.h>
#include <adapta.h>

#include <memory>

#include "app.h"
extern "C" {
#include "catalog_resources.h"
}

static void on_activate(GtkApplication *app, gpointer) {
    auto *window = new catalog::CatalogWindow(app);
    g_signal_connect(G_OBJECT(window->widget()), "destroy",
                     G_CALLBACK(+[](GtkWidget *, gpointer data) {
                         delete static_cast<catalog::CatalogWindow *>(data);
                     }),
                     window);
    gtk_window_present(GTK_WINDOW(window->widget()));
}

int main(int argc, char **argv) {
    GtkApplication *app = GTK_APPLICATION(
        adw_application_new("io.univ.Catalog", G_APPLICATION_DEFAULT_FLAGS));
    g_resources_register(catalog_get_resource());
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), nullptr);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
