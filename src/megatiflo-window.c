#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include <libsoup/soup.h>
#include "megatiflo-window.h"

struct _MegatifloWindow
{
  GtkApplicationWindow  parent_instance;
  GtkButton            *refresh_button;
  GtkComboBox          *category_combo;
  GtkListStore         *category_store;
  GtkSearchEntry       *search_entry;
  GtkListStore         *films_store;
  GtkTreeModelFilter   *films_filter;
  GtkTreeView          *films_tree;
};

enum
{
  CATEGORY_NEW,
  CATEGORY_SAVED,
  CATEGORY_ALL  
};

enum
{
  CATEGORY_COLUMN_TITLE,
  CATEGORY_COLUMN_COUNT
};

enum
{
  COLUMN_SAVED,
  COLUMN_TITLE
};

#define FILE_NAME "megatiflo"

G_DEFINE_TYPE (MegatifloWindow, megatiflo_window, GTK_TYPE_APPLICATION_WINDOW)

static void 
refilter_films (GtkTreeView *film_tree)
{
  GtkTreeModelFilter *films_filter;
  GtkTreePath *path;

  films_filter = GTK_TREE_MODEL_FILTER (gtk_tree_view_get_model (film_tree));
  path = gtk_tree_path_new_first ();

  gtk_tree_model_filter_refilter (films_filter);
  gtk_tree_selection_select_path (gtk_tree_view_get_selection (film_tree), path);

  gtk_tree_path_free (path);
}

static gboolean 
films_filter_func (GtkTreeModel *films_store, 
                   GtkTreeIter  *iter, 
                   gpointer      data)
{
  MegatifloWindow *self = data;
  gboolean saved;
  gchar *title;
  const gchar *search;
  gint text_len;
  gboolean match;
  
  gtk_tree_model_get (films_store, iter, 
                      COLUMN_SAVED, &saved,
                      COLUMN_TITLE, &title,
                      -1);
  
  if (!title)
    return FALSE;
  
  search = gtk_entry_get_text (GTK_ENTRY (self->search_entry));
  text_len = gtk_entry_get_text_length (GTK_ENTRY (self->search_entry)); 
  match = g_str_match_string (search, title, TRUE); 
  
  g_free (title);
  
  if (text_len > 0 && !match)
    return FALSE;
  
  switch (gtk_combo_box_get_active (self->category_combo))
    {
    case CATEGORY_NEW:
      return !saved;
    case CATEGORY_SAVED:
      return saved;
    default:
      return TRUE;
    }
}

static char *
load_page (gint page_num)
{
  GString *url_string;
  char *url_template;
  char *url;
  SoupMessage *message;
  SoupMessageBody *body; 
  SoupSession *session;
  char *page;
  
  url_string = g_string_new (NULL);
  url_template = "https://megogo.net/ru/search-extended"
                 "?page=%d"
                 "&q=%%D1%%82%%D0%%B8%%D1%%84%%D0%%BB%%D0%%BE"
                 "&tab=video";

  g_string_printf (url_string, url_template, page_num);

  url = g_string_free (url_string, FALSE);
  session = soup_session_new ();
  message = soup_message_new (SOUP_METHOD_GET, url);

  soup_session_send_message (session, message);
  g_object_get (message, 
                "response-body", &body, 
                NULL);

  page = g_strdup (body->data);
  
  g_free (url);
  g_object_unref (session);
  g_object_unref (message);
  soup_message_body_free (body);
  
  return page;
}

static void 
load_films (GTask        *task,
            gpointer      source_object,
            gpointer      task_data,
            GCancellable *cancellable)
{
  char *page;
  GRegex *regex;
  GMatchInfo *match_info;
  GList *films = NULL;
  gboolean match;

  regex = g_regex_new ("<[^>]+class\\s*=\\s*[\'\"]\\s*video-title[^>]+>"
                       "\\s*([^\\s][^(]+[^\\s(])\\s*\\(\\s*версия",
                       G_REGEX_CASELESS, 0, NULL);

  for (int page_num = 1;; page_num++)
    {
      if (!(page = load_page (page_num)))
        {
          g_list_free_full (films, g_free);

          break;
        }

      match = g_regex_match (regex, page, 0, &match_info);

      while (g_match_info_matches (match_info))
        {
          films = g_list_append (films, g_match_info_fetch (match_info, 1));

          g_match_info_next (match_info, NULL);
        }
      
      g_match_info_free (match_info);
      g_free (page);
      
      if (!match)
          break;
    }
  
  g_regex_unref (regex);
  
  g_task_return_pointer (task, films, NULL);
} 

static void
add_film_func (gpointer data, gpointer user_data)
{
  GtkTreeModel *films_model = GTK_TREE_MODEL (user_data);
  gchar *new_film = data;
  gchar *old_film;
  GtkTreeIter iter;
  gboolean equal;

  if (gtk_tree_model_get_iter_first (films_model, &iter))
    {
      do
        {
          gtk_tree_model_get (films_model, &iter,
                              COLUMN_TITLE, &old_film,
                              -1);

          equal = g_str_equal (new_film, old_film);

          g_free (old_film);
          
          if (equal)
            return;
        }
      while (gtk_tree_model_iter_next (films_model, &iter)); 
    }

  gtk_list_store_append (GTK_LIST_STORE (films_model), &iter);
  gtk_list_store_set (GTK_LIST_STORE (films_model), &iter,
                      COLUMN_SAVED, FALSE,
                      COLUMN_TITLE, new_film,
                      -1);
}

static void
show_error_message (MegatifloWindow *self)
{
  GtkWidget *dialog;

  dialog = gtk_message_dialog_new (GTK_WINDOW (self),
                                   GTK_DIALOG_MODAL, 
                                   GTK_MESSAGE_ERROR, 
                                   GTK_BUTTONS_CLOSE, 
                                   "Не удалось загрузить фильмы");

  gtk_dialog_run (GTK_DIALOG (dialog));
  gtk_widget_destroy (dialog);
}

static void
update_counts (MegatifloWindow *self)
{
  GtkTreeModel *films_model = GTK_TREE_MODEL (self->films_store);
  GtkTreeModel *category_model = GTK_TREE_MODEL (self->category_store);
  GtkTreeIter iter;
  gboolean saved;
  gchar *title;
  gint new_count = 0;
  gint saved_count = 0;
  gint all_count = 0;
  GString *count;

  if (gtk_tree_model_get_iter_first (films_model, &iter))
    {
      do
        {
          gtk_tree_model_get (films_model, &iter,
                              COLUMN_SAVED, &saved,
                              COLUMN_TITLE, &title,
                              -1);

          all_count++;

          if (saved)
            saved_count++;
          else
            new_count++;

          g_free (title);
        }
      while (gtk_tree_model_iter_next (films_model, &iter));
    }

  count = g_string_new (NULL);
  
  gtk_tree_model_get_iter_first (category_model, &iter);

  g_string_printf (count, "%d", new_count);
  gtk_list_store_set (self->category_store, &iter,
                      CATEGORY_COLUMN_COUNT, count->str,
                      -1);

  gtk_tree_model_iter_next (category_model, &iter);
  g_string_printf (count, "%d", saved_count);
  gtk_list_store_set (self->category_store, &iter,
                      CATEGORY_COLUMN_COUNT, count->str,
                      -1);

  gtk_tree_model_iter_next (category_model, &iter);
  g_string_printf (count, "%d", all_count);
  gtk_list_store_set (self->category_store, &iter,
                      CATEGORY_COLUMN_COUNT, count->str,
                      -1);
  
  g_string_free (count, TRUE);
}

static void
films_loaded (GObject      *source_object,
              GAsyncResult *res,
              gpointer      user_data)
{  
  MegatifloWindow *self = MEGATIFLO_WINDOW (source_object);
  GList *films;
  
  if ((films = g_task_propagate_pointer (G_TASK (res), NULL)))
    {
      g_list_foreach (films, add_film_func, self->films_store);
      update_counts (self);
      
      g_list_free_full (films, g_free);
    }
  else
    {
      show_error_message (self);
    }

  gtk_widget_set_sensitive (GTK_WIDGET (self->refresh_button), TRUE);
}

static void
save_films (GtkListStore *films_store)
{
  GtkTreeModel *films_model = GTK_TREE_MODEL (films_store);
  GtkTreeIter iter;
  GString *string;
  gchar *film;
  gchar *contents;
  gboolean saved;
  
  if (!gtk_tree_model_get_iter_first (films_model, &iter))
    return;
   
  string = g_string_new (NULL);
  
  do
    {
      gtk_tree_model_get (films_model, &iter,
                          COLUMN_SAVED, &saved,
                          COLUMN_TITLE, &film,
                          -1);

      if (saved)
        g_string_append_printf (string, "%s\n", film);

      g_free (film);
    }
  while (gtk_tree_model_iter_next (films_model, &iter));
  
  g_string_truncate (string, string->len - strlen ("\n"));
  contents = g_string_free (string, FALSE);
  
  g_chdir (g_get_user_data_dir ());
  g_file_set_contents (FILE_NAME, contents, -1, NULL);
  
  g_free (contents);
}


static void
restore_films (GtkListStore *films_store)
{
  gchar *contents;
  gchar **films;
  GtkTreeIter iter;

  g_chdir (g_get_user_data_dir ());
  g_file_get_contents (FILE_NAME, &contents, NULL, NULL);

  if (!contents)
    return;

  films = g_strsplit (contents, "\n", -1);

  for (gint i = 0; films[i] != NULL; i++)
    {
      gtk_list_store_append (films_store, &iter);
      gtk_list_store_set (films_store, &iter,
                          COLUMN_SAVED, TRUE,
                          COLUMN_TITLE, films[i],
                          -1);
    }
  
  g_free (contents);
  g_strfreev (films); 
}

static void
on_refresh_button_clicked (GtkWidget       *refresh_button,
                           MegatifloWindow *self)
{
  GTask *task;
  
  task = g_task_new (self, NULL, films_loaded, NULL);
 
  gtk_widget_set_sensitive (refresh_button, FALSE);   
  g_task_run_in_thread (task, load_films);
  
  g_object_unref (task);
}

static void
on_category_combo_changed (GtkTreeView *films_tree)
{
  refilter_films (films_tree);
}

static void 
on_search_entry_search_changed (GtkTreeView *films_tree)
{
  refilter_films (films_tree);
}

static void
on_films_tree_row_activated (GtkTreeView       *films_tree, 
                             GtkTreePath       *path,
                             GtkTreeViewColumn *column,
                             MegatifloWindow   *self)
{  
  GtkTreeModelFilter *filter;
  GtkTreeModel *model;
  GtkTreeIter filter_iter;
  GtkTreeIter model_iter;
  gboolean saved;
  
  if (gtk_combo_box_get_active (self->category_combo) == CATEGORY_ALL)
    return;
  
  filter = GTK_TREE_MODEL_FILTER (gtk_tree_view_get_model (films_tree));
  model = gtk_tree_model_filter_get_model (filter);
  
  gtk_tree_model_get_iter (GTK_TREE_MODEL (filter), &filter_iter, path);
  gtk_tree_model_get (GTK_TREE_MODEL (filter), &filter_iter, 
                      COLUMN_SAVED, &saved,
                      -1);
  gtk_tree_model_filter_convert_iter_to_child_iter (filter, 
                                                    &model_iter, 
                                                    &filter_iter);
  gtk_list_store_set (GTK_LIST_STORE (model), &model_iter, 
                      COLUMN_SAVED, !saved,
                      -1);
  save_films (GTK_LIST_STORE (model));
  update_counts (self);
}

static void
megatiflo_window_class_init (MegatifloWindowClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  gtk_widget_class_set_template_from_resource (widget_class, "/megatiflo-window.ui");
  
  gtk_widget_class_bind_template_child (widget_class, MegatifloWindow, refresh_button);
  gtk_widget_class_bind_template_child (widget_class, MegatifloWindow, category_combo);
  gtk_widget_class_bind_template_child (widget_class, MegatifloWindow, category_store);
  gtk_widget_class_bind_template_child (widget_class, MegatifloWindow, search_entry);
  gtk_widget_class_bind_template_child (widget_class, MegatifloWindow, films_store);
  gtk_widget_class_bind_template_child (widget_class, MegatifloWindow, films_filter);
  gtk_widget_class_bind_template_child (widget_class, MegatifloWindow, films_tree);

  gtk_widget_class_bind_template_callback (widget_class, on_refresh_button_clicked);
  gtk_widget_class_bind_template_callback (widget_class, on_search_entry_search_changed);
  gtk_widget_class_bind_template_callback (widget_class, on_category_combo_changed);
  gtk_widget_class_bind_template_callback (widget_class, on_films_tree_row_activated);
}

static void
megatiflo_window_init (MegatifloWindow *self)
{
  GdkPixbuf *pixbuf;

  pixbuf = gdk_pixbuf_new_from_resource ("/com.github.andrejgricenko.megatiflo.png", NULL);
  
  gtk_widget_init_template (GTK_WIDGET (self));
  gtk_window_set_icon (GTK_WINDOW (self), pixbuf);
  gtk_tree_model_filter_set_visible_func (self->films_filter, 
                                          films_filter_func, 
                                          self, 
                                          NULL);
  restore_films (self->films_store);
  refilter_films (self->films_tree);
  update_counts (self);

  g_object_unref (pixbuf);
}

GtkWidget *
megatiflo_window_new (GtkApplication *application)
{
  return g_object_new (MEGATIFLO_TYPE_WINDOW, 
                       "application", application, 
                       NULL);
}
