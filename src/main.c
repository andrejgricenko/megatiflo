#include <gtk/gtk.h>
#include "megatiflo-window.h"

void
on_activated (GtkApplication *application)
{
  GtkWidget *window = megatiflo_window_new (application);
  gtk_window_present (GTK_WINDOW (window));
}

int
main (int    argc,
      char **argv)
{
  GtkApplication *application = gtk_application_new ("com.github.andrejgricenko.megatiflo", 
                                                     G_APPLICATION_FLAGS_NONE);
  
  g_signal_connect (application, "activate", G_CALLBACK (on_activated), NULL);
  return g_application_run (G_APPLICATION (application), argc, argv);
}
