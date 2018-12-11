#ifndef MEGATIFLO_WINDOW_H
#define MEGATIFLO_WINDOW_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define MEGATIFLO_TYPE_WINDOW (megatiflo_window_get_type ())

G_DECLARE_FINAL_TYPE (MegatifloWindow, megatiflo_window, MEGATIFLO, WINDOW, GtkApplicationWindow);

GtkWidget *megatiflo_window_new (GtkApplication *application);

G_END_DECLS

#endif
