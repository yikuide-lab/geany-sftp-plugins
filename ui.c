/*
 * UI Module
 * Create user interface with GTK+
 */

#include "sftp-plugin.h"
#include <gdk/gdkkeysyms.h>
#include <time.h>

/* Forward declarations */
static void download_and_open_file(SFTPPluginData *plugin_data, const gchar *filename);
static void navigate_to_directory(SFTPPluginData *plugin_data, const gchar *dirname);
static void navigate_to_path(SFTPPluginData *plugin_data, const gchar *path);

/* Context for async upload callback */
typedef struct {
    SFTPPluginData *plugin_data;
    gchar remote_path[MAX_PATH_LEN];
} UploadCtx;

/* Context for async download-and-open callback */
typedef struct {
    SFTPPluginData *plugin_data;
    gchar local_path[MAX_PATH_LEN];
    gchar remote_path[MAX_PATH_LEN];
    gchar filename[MAX_PATH_LEN];
} DownloadOpenCtx;

/* Context for async download-to-file callback */
typedef struct {
    SFTPPluginData *plugin_data;
    gchar local_path[MAX_PATH_LEN];
} DownloadSaveCtx;

static void on_upload_complete(FileOperation *op, gboolean success, gpointer user_data)
{
    UploadCtx *ctx = (UploadCtx *)user_data;
    if (success) {
        dialogs_show_msgbox(GTK_MESSAGE_INFO, "Upload success: %s", ctx->remote_path);
        ui_update_file_list(ctx->plugin_data);
    } else {
        dialogs_show_msgbox(GTK_MESSAGE_ERROR, "Upload failed");
    }
    /* Re-enable UI */
    gtk_widget_set_sensitive(ctx->plugin_data->upload_btn, TRUE);
    gtk_widget_set_sensitive(ctx->plugin_data->refresh_btn, TRUE);
    gtk_widget_set_sensitive(ctx->plugin_data->file_treeview, TRUE);
    g_free(ctx);
    g_thread_unref(op->thread);
    g_free(op);
}

static void on_download_open_complete(FileOperation *op, gboolean success, gpointer user_data)
{
    DownloadOpenCtx *ctx = (DownloadOpenCtx *)user_data;
    if (success) {
        g_hash_table_insert(ctx->plugin_data->downloaded_files,
                            g_strdup(ctx->local_path), g_strdup(ctx->remote_path));
        document_open_file(ctx->local_path, FALSE, NULL, NULL);
        g_print("Opened file: %s (remote: %s)\n", ctx->local_path, ctx->remote_path);
    } else {
        dialogs_show_msgbox(GTK_MESSAGE_ERROR, "Failed to download: %s", ctx->filename);
    }
    gtk_widget_set_sensitive(ctx->plugin_data->upload_btn, TRUE);
    gtk_widget_set_sensitive(ctx->plugin_data->refresh_btn, TRUE);
    gtk_widget_set_sensitive(ctx->plugin_data->file_treeview, TRUE);
    g_free(ctx);
    g_thread_unref(op->thread);
    g_free(op);
}

static void on_download_save_complete(FileOperation *op, gboolean success, gpointer user_data)
{
    DownloadSaveCtx *ctx = (DownloadSaveCtx *)user_data;
    if (success)
        dialogs_show_msgbox(GTK_MESSAGE_INFO, "Downloaded: %s", ctx->local_path);
    else
        dialogs_show_msgbox(GTK_MESSAGE_ERROR, "Download failed");
    gtk_widget_set_sensitive(ctx->plugin_data->upload_btn, TRUE);
    gtk_widget_set_sensitive(ctx->plugin_data->refresh_btn, TRUE);
    gtk_widget_set_sensitive(ctx->plugin_data->file_treeview, TRUE);
    g_free(ctx);
    g_thread_unref(op->thread);
    g_free(op);
}

/*
 * Update connection combo box
 */
static void update_connection_combo(SFTPPluginData *plugin_data)
{
    int i;

    if (!plugin_data->connection_combo)
        return;

    /* Clear list - use gtk_combo_box_text_remove_all for GtkComboBoxText */
    gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(plugin_data->connection_combo));

    /* Add connections */
    for (i = 0; i < plugin_data->num_connections; i++) {
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(plugin_data->connection_combo),
                                       plugin_data->connections[i].name);
    }

    if (plugin_data->num_connections > 0) {
        gtk_combo_box_set_active(GTK_COMBO_BOX(plugin_data->connection_combo), 0);
    }
}

/* Public function to update connection combo */
void ui_update_connection_combo(SFTPPluginData *plugin_data)
{
    update_connection_combo(plugin_data);
}

/*
 * Connection combo changed callback
 */
static void on_connection_changed(GtkWidget *widget, gpointer data)
{
    SFTPPluginData *plugin_data = (SFTPPluginData *)data;
    gint active;

    (void)widget;
    active = gtk_combo_box_get_active(GTK_COMBO_BOX(plugin_data->connection_combo));
    if (active >= 0 && active < plugin_data->num_connections) {
        plugin_data->current_connection = active;

        /* Update button label based on connection state */
        if (plugin_data->sessions[active] && plugin_data->sessions[active]->active) {
            gtk_button_set_label(GTK_BUTTON(plugin_data->connect_btn), "Disconnect");
        } else {
            gtk_button_set_label(GTK_BUTTON(plugin_data->connect_btn), "Connect");
        }

        g_print("Selected: %s\n", plugin_data->connections[active].name);
    }
}

/*
 * Connect button clicked callback
 */
static void on_connect_clicked(GtkWidget *widget, gpointer data)
{
    SFTPPluginData *plugin_data = (SFTPPluginData *)data;
    SFTPConnection *conn;
    SFTPSession *session;

    (void)widget;

    if (plugin_data->current_connection < 0) {
        dialogs_show_msgbox(GTK_MESSAGE_WARNING, "Please select a connection first");
        return;
    }

    conn = &plugin_data->connections[plugin_data->current_connection];

    /* Check if already connected */
    if (plugin_data->sessions[plugin_data->current_connection] &&
        plugin_data->sessions[plugin_data->current_connection]->active) {
        /* Disconnect */
        sftp_connection_disconnect(plugin_data->sessions[plugin_data->current_connection]);
        g_mutex_clear(&plugin_data->sessions[plugin_data->current_connection]->lock);
        g_free(plugin_data->sessions[plugin_data->current_connection]);
        plugin_data->sessions[plugin_data->current_connection] = NULL;

        /* Clear file list and path */
        GtkListStore *list_store = GTK_LIST_STORE(
            gtk_tree_view_get_model(GTK_TREE_VIEW(plugin_data->file_treeview)));
        gtk_list_store_clear(list_store);
        gtk_entry_set_text(GTK_ENTRY(plugin_data->path_entry), "/");
        strcpy(plugin_data->current_remote_path, "/");

        /* Update button label */
        gtk_button_set_label(GTK_BUTTON(plugin_data->connect_btn), "Connect");

        g_print("Disconnected from %s\n", conn->name);
        return;
    }

    /* Create new session */
    session = g_new0(SFTPSession, 1);
    session->config = conn;
    session->sock = 0;
    session->ssh_session = NULL;
    session->sftp_session = NULL;
    session->active = FALSE;
    g_mutex_init(&session->lock);

    /* Connect */
    if (sftp_connection_connect(session)) {
        /* Create temp directory for this session */
        g_snprintf(session->temp_dir, sizeof(session->temp_dir),
                 "%s/geany_sftp_%s_%d", g_get_tmp_dir(), conn->name, (int)time(NULL));
        g_mkdir_with_parents(session->temp_dir, 0755);

        plugin_data->sessions[plugin_data->current_connection] = session;
        strcpy(plugin_data->current_remote_path, conn->remote_dir);

        /* Update file list */
        ui_update_file_list(plugin_data);

        /* Update button label */
        gtk_button_set_label(GTK_BUTTON(plugin_data->connect_btn), "Disconnect");

        g_print("Connected to %s (temp: %s)\n", conn->name, session->temp_dir);
    } else {
        g_free(session);
        dialogs_show_msgbox(GTK_MESSAGE_ERROR, "Connection failed");
    }
}

/*
 * Refresh button clicked callback
 */
static void on_refresh_clicked(GtkWidget *widget, gpointer data)
{
    SFTPPluginData *plugin_data = (SFTPPluginData *)data;

    (void)widget;

    if (plugin_data->current_connection < 0 ||
        !plugin_data->sessions[plugin_data->current_connection]) {
        dialogs_show_msgbox(GTK_MESSAGE_WARNING, "Not connected to server");
        return;
    }

    ui_update_file_list(plugin_data);
}

/*
 * Upload button clicked callback
 */
static void on_upload_clicked(GtkWidget *widget, gpointer data)
{
    SFTPPluginData *plugin_data = (SFTPPluginData *)data;
    GeanyDocument *doc;
    SFTPSession *session;
    gchar remote_path[MAX_PATH_LEN];

    (void)widget;

    if (plugin_data->current_connection < 0 ||
        !plugin_data->sessions[plugin_data->current_connection] ||
        !plugin_data->sessions[plugin_data->current_connection]->active) {
        dialogs_show_msgbox(GTK_MESSAGE_WARNING, "Please connect to server first");
        return;
    }

    doc = document_get_current();
    if (!doc || !doc->file_name) {
        dialogs_show_msgbox(GTK_MESSAGE_WARNING, "Please open a file first");
        return;
    }

    session = plugin_data->sessions[plugin_data->current_connection];
    g_snprintf(remote_path, sizeof(remote_path), "%s/%s",
             plugin_data->current_remote_path, g_path_get_basename(doc->file_name));

    UploadCtx *ctx = g_new0(UploadCtx, 1);
    ctx->plugin_data = plugin_data;
    g_strlcpy(ctx->remote_path, remote_path, MAX_PATH_LEN);
    gtk_widget_set_sensitive(plugin_data->upload_btn, FALSE);
    gtk_widget_set_sensitive(plugin_data->refresh_btn, FALSE);
    gtk_widget_set_sensitive(plugin_data->file_treeview, FALSE);
    FileOperation *op = transfer_async(session, doc->file_name, remote_path, TRUE,
                                       on_upload_complete, ctx);
    ui_show_progress_dialog(plugin_data, op);
}

/*
 * Get selected filename and type from tree view
 */
static gboolean get_selected_file(SFTPPluginData *plugin_data, gchar **filename, gchar **type)
{
    GtkTreeSelection *selection;
    GtkTreeModel *model;
    GtkTreeIter iter;

    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(plugin_data->file_treeview));
    if (!gtk_tree_selection_get_selected(selection, &model, &iter))
        return FALSE;

    gtk_tree_model_get(model, &iter, 0, filename, 1, type, -1);
    return TRUE;
}

/*
 * Navigate to directory
 */
static void navigate_to_directory(SFTPPluginData *plugin_data, const gchar *dirname)
{
    if (strcmp(dirname, "..") == 0) {
        /* Go up one level */
        gchar *last_slash = strrchr(plugin_data->current_remote_path, '/');
        if (last_slash && last_slash != plugin_data->current_remote_path) {
            *last_slash = '\0';
        } else {
            strcpy(plugin_data->current_remote_path, "/");
        }
    } else if (strcmp(dirname, ".") != 0) {
        /* Navigate into directory */
        gchar new_path[MAX_PATH_LEN];
        if (strcmp(plugin_data->current_remote_path, "/") == 0) {
            g_snprintf(new_path, sizeof(new_path), "/%s", dirname);
        } else {
            g_snprintf(new_path, sizeof(new_path), "%s/%s",
                     plugin_data->current_remote_path, dirname);
        }
        g_strlcpy(plugin_data->current_remote_path, new_path, MAX_PATH_LEN);
    }
    ui_update_file_list(plugin_data);
}

/*
 * Navigate to absolute path
 */
static void navigate_to_path(SFTPPluginData *plugin_data, const gchar *path)
{
    if (!path || strlen(path) == 0)
        return;

    /* Ensure path starts with / */
    if (path[0] != '/') {
        gchar new_path[MAX_PATH_LEN];
        g_snprintf(new_path, sizeof(new_path), "/%s", path);
        g_strlcpy(plugin_data->current_remote_path, new_path, MAX_PATH_LEN);
    } else {
        g_strlcpy(plugin_data->current_remote_path, path, MAX_PATH_LEN);
    }
    ui_update_file_list(plugin_data);
}

/*
 * Path entry activated (Enter pressed)
 */
static void on_path_entry_activated(GtkEntry *entry, gpointer data)
{
    SFTPPluginData *plugin_data = (SFTPPluginData *)data;
    const gchar *path;

    /* Check if connected */
    if (plugin_data->current_connection < 0 ||
        !plugin_data->sessions[plugin_data->current_connection] ||
        !plugin_data->sessions[plugin_data->current_connection]->active) {
        dialogs_show_msgbox(GTK_MESSAGE_WARNING, "Not connected to server");
        return;
    }

    path = gtk_entry_get_text(entry);
    navigate_to_path(plugin_data, path);
}

/*
 * Download file and open in Geany
 */
static void download_and_open_file(SFTPPluginData *plugin_data, const gchar *filename)
{
    SFTPSession *session;
    gchar remote_path[MAX_PATH_LEN];
    gchar local_path[MAX_PATH_LEN];

    if (plugin_data->current_connection < 0 ||
        !plugin_data->sessions[plugin_data->current_connection])
        return;

    session = plugin_data->sessions[plugin_data->current_connection];

    /* Build remote path */
    if (strcmp(plugin_data->current_remote_path, "/") == 0) {
        g_snprintf(remote_path, sizeof(remote_path), "/%s", filename);
    } else {
        g_snprintf(remote_path, sizeof(remote_path), "%s/%s",
                 plugin_data->current_remote_path, filename);
    }

    /* Build local path in session temp directory */
    g_snprintf(local_path, sizeof(local_path), "%s/%s", session->temp_dir, filename);

    /* Download file async */
    DownloadOpenCtx *ctx = g_new0(DownloadOpenCtx, 1);
    ctx->plugin_data = plugin_data;
    g_strlcpy(ctx->local_path, local_path, MAX_PATH_LEN);
    g_strlcpy(ctx->remote_path, remote_path, MAX_PATH_LEN);
    g_strlcpy(ctx->filename, filename, MAX_PATH_LEN);
    gtk_widget_set_sensitive(plugin_data->upload_btn, FALSE);
    gtk_widget_set_sensitive(plugin_data->refresh_btn, FALSE);
    gtk_widget_set_sensitive(plugin_data->file_treeview, FALSE);
    FileOperation *op = transfer_async(session, local_path, remote_path, FALSE,
                                       on_download_open_complete, ctx);
    ui_show_progress_dialog(plugin_data, op);
}

/*
 * Double-click handler for file list
 */
static void on_file_row_activated(GtkTreeView *tree_view, GtkTreePath *path,
                                   GtkTreeViewColumn *column, gpointer data)
{
    SFTPPluginData *plugin_data = (SFTPPluginData *)data;
    GtkTreeModel *model;
    GtkTreeIter iter;
    gchar *filename, *type;

    (void)column;

    model = gtk_tree_view_get_model(tree_view);
    if (!gtk_tree_model_get_iter(model, &iter, path))
        return;

    gtk_tree_model_get(model, &iter, 0, &filename, 1, &type, -1);

    if (strcmp(type, "DIR") == 0) {
        navigate_to_directory(plugin_data, filename);
    } else {
        download_and_open_file(plugin_data, filename);
    }

    g_free(filename);
    g_free(type);
}

/*
 * Context menu callbacks
 */
static void on_menu_open(GtkMenuItem *item, gpointer data)
{
    SFTPPluginData *plugin_data = (SFTPPluginData *)data;
    gchar *filename, *type;
    (void)item;

    if (get_selected_file(plugin_data, &filename, &type)) {
        if (strcmp(type, "DIR") == 0) {
            navigate_to_directory(plugin_data, filename);
        } else {
            download_and_open_file(plugin_data, filename);
        }
        g_free(filename);
        g_free(type);
    }
}

static void on_menu_download(GtkMenuItem *item, gpointer data)
{
    SFTPPluginData *plugin_data = (SFTPPluginData *)data;
    SFTPSession *session;
    gchar *filename, *type;
    gchar remote_path[MAX_PATH_LEN];
    GtkWidget *dialog;
    gchar *local_path;
    (void)item;

    if (!get_selected_file(plugin_data, &filename, &type))
        return;

    if (strcmp(type, "DIR") == 0) {
        g_free(filename);
        g_free(type);
        dialogs_show_msgbox(GTK_MESSAGE_INFO, "Directory download not supported yet");
        return;
    }

    session = plugin_data->sessions[plugin_data->current_connection];

    /* Choose save location */
    dialog = gtk_file_chooser_dialog_new("Save File", NULL,
                                          GTK_FILE_CHOOSER_ACTION_SAVE,
                                          "_Cancel", GTK_RESPONSE_CANCEL,
                                          "_Save", GTK_RESPONSE_ACCEPT, NULL);
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog), filename);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        local_path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));

        if (strcmp(plugin_data->current_remote_path, "/") == 0) {
            g_snprintf(remote_path, sizeof(remote_path), "/%s", filename);
        } else {
            g_snprintf(remote_path, sizeof(remote_path), "%s/%s",
                     plugin_data->current_remote_path, filename);
        }

        DownloadSaveCtx *ctx = g_new0(DownloadSaveCtx, 1);
        ctx->plugin_data = plugin_data;
        g_strlcpy(ctx->local_path, local_path, MAX_PATH_LEN);
        gtk_widget_set_sensitive(plugin_data->upload_btn, FALSE);
        gtk_widget_set_sensitive(plugin_data->refresh_btn, FALSE);
        gtk_widget_set_sensitive(plugin_data->file_treeview, FALSE);
        FileOperation *fop = transfer_async(session, local_path, remote_path, FALSE,
                                            on_download_save_complete, ctx);
        ui_show_progress_dialog(plugin_data, fop);
        g_free(local_path);
    }

    gtk_widget_destroy(dialog);
    g_free(filename);
    g_free(type);
}

static void on_menu_delete(GtkMenuItem *item, gpointer data)
{
    SFTPPluginData *plugin_data = (SFTPPluginData *)data;
    SFTPSession *session;
    gchar *filename, *type;
    gchar remote_path[MAX_PATH_LEN];
    int rc;
    (void)item;

    if (!get_selected_file(plugin_data, &filename, &type))
        return;

    if (!dialogs_show_question("Delete '%s'?", filename)) {
        g_free(filename);
        g_free(type);
        return;
    }

    session = plugin_data->sessions[plugin_data->current_connection];

    if (strcmp(plugin_data->current_remote_path, "/") == 0) {
        g_snprintf(remote_path, sizeof(remote_path), "/%s", filename);
    } else {
        g_snprintf(remote_path, sizeof(remote_path), "%s/%s",
                 plugin_data->current_remote_path, filename);
    }

    if (strcmp(type, "DIR") == 0) {
        rc = libssh2_sftp_rmdir(session->sftp_session, remote_path);
    } else {
        rc = libssh2_sftp_unlink(session->sftp_session, remote_path);
    }

    if (rc == 0) {
        dialogs_show_msgbox(GTK_MESSAGE_INFO, "Deleted: %s", filename);
        ui_update_file_list(plugin_data);
    } else {
        dialogs_show_msgbox(GTK_MESSAGE_ERROR, "Delete failed (may not be empty)");
    }

    g_free(filename);
    g_free(type);
}

static void on_menu_mkdir(GtkMenuItem *item, gpointer data)
{
    SFTPPluginData *plugin_data = (SFTPPluginData *)data;
    SFTPSession *session;
    gchar remote_path[MAX_PATH_LEN];
    gchar *dirname;
    (void)item;

    dirname = dialogs_show_input("Create Directory", NULL, "Folder name:", "New Folder");
    if (!dirname || strlen(dirname) == 0) {
        g_free(dirname);
        return;
    }

    session = plugin_data->sessions[plugin_data->current_connection];

    if (strcmp(plugin_data->current_remote_path, "/") == 0) {
        g_snprintf(remote_path, sizeof(remote_path), "/%s", dirname);
    } else {
        g_snprintf(remote_path, sizeof(remote_path), "%s/%s",
                 plugin_data->current_remote_path, dirname);
    }

    if (libssh2_sftp_mkdir(session->sftp_session, remote_path,
                           LIBSSH2_SFTP_S_IRWXU | LIBSSH2_SFTP_S_IRGRP |
                           LIBSSH2_SFTP_S_IXGRP | LIBSSH2_SFTP_S_IROTH |
                           LIBSSH2_SFTP_S_IXOTH) == 0) {
        dialogs_show_msgbox(GTK_MESSAGE_INFO, "Created: %s", dirname);
        ui_update_file_list(plugin_data);
    } else {
        dialogs_show_msgbox(GTK_MESSAGE_ERROR, "Failed to create directory");
    }

    g_free(dirname);
}

/*
 * Show context menu on right-click
 */
static gboolean on_file_button_press(GtkWidget *widget, GdkEventButton *event, gpointer data)
{
    SFTPPluginData *plugin_data = (SFTPPluginData *)data;
    GtkWidget *menu, *item;
    GtkTreePath *path;
    GtkTreeSelection *selection;

    if (event->type != GDK_BUTTON_PRESS || event->button != 3)
        return FALSE;

    /* Select row under cursor */
    if (gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget),
                                       (gint)event->x, (gint)event->y,
                                       &path, NULL, NULL, NULL)) {
        selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(widget));
        gtk_tree_selection_select_path(selection, path);
        gtk_tree_path_free(path);
    }

    /* Create popup menu */
    menu = gtk_menu_new();

    item = gtk_menu_item_new_with_label("Open");
    g_signal_connect(item, "activate", G_CALLBACK(on_menu_open), plugin_data);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

    item = gtk_menu_item_new_with_label("Download...");
    g_signal_connect(item, "activate", G_CALLBACK(on_menu_download), plugin_data);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

    item = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

    item = gtk_menu_item_new_with_label("New Folder...");
    g_signal_connect(item, "activate", G_CALLBACK(on_menu_mkdir), plugin_data);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

    item = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

    item = gtk_menu_item_new_with_label("Delete");
    g_signal_connect(item, "activate", G_CALLBACK(on_menu_delete), plugin_data);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

    gtk_widget_show_all(menu);
    gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *)event);

    return TRUE;
}

/*
 * Create sidebar
 */
void ui_create_sidebar(SFTPPluginData *plugin_data)
{
    GtkWidget *sidebar_vbox;
    GtkWidget *connection_frame;
    GtkWidget *connection_vbox;
    GtkWidget *browser_frame;
    GtkWidget *toolbar;
    GtkWidget *scrolled_window;
    GtkListStore *list_store;
    GtkTreeViewColumn *column;
    GtkCellRenderer *renderer;

    /* Main container */
    sidebar_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_show(sidebar_vbox);

    /* Connection frame */
    connection_frame = gtk_frame_new("Connection");
    gtk_widget_show(connection_frame);

    /* Connection row - combo and button on same line */
    connection_vbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
    gtk_widget_show(connection_vbox);

    /* Connection selector */
    plugin_data->connection_combo = gtk_combo_box_text_new();
    gtk_widget_show(plugin_data->connection_combo);
    g_signal_connect(plugin_data->connection_combo, "changed",
                     G_CALLBACK(on_connection_changed), plugin_data);
    gtk_box_pack_start(GTK_BOX(connection_vbox), plugin_data->connection_combo,
                       TRUE, TRUE, 0);

    /* Connect button */
    plugin_data->connect_btn = gtk_button_new_with_label("Connect");
    gtk_widget_show(plugin_data->connect_btn);
    g_signal_connect(plugin_data->connect_btn, "clicked", G_CALLBACK(on_connect_clicked), plugin_data);
    gtk_box_pack_start(GTK_BOX(connection_vbox), plugin_data->connect_btn, FALSE, FALSE, 0);

    gtk_container_add(GTK_CONTAINER(connection_frame), connection_vbox);
    gtk_box_pack_start(GTK_BOX(sidebar_vbox), connection_frame, FALSE, FALSE, 0);

    /* File browser frame */
    browser_frame = gtk_frame_new("Remote Files");
    gtk_widget_show(browser_frame);

    /* Browser content box */
    GtkWidget *browser_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_show(browser_vbox);

    /* Toolbar */
    toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_widget_show(toolbar);

    /* Refresh button */
    plugin_data->refresh_btn = gtk_button_new_with_label("Refresh");
    gtk_widget_show(plugin_data->refresh_btn);
    g_signal_connect(plugin_data->refresh_btn, "clicked", G_CALLBACK(on_refresh_clicked), plugin_data);
    gtk_box_pack_start(GTK_BOX(toolbar), plugin_data->refresh_btn, FALSE, FALSE, 0);

    /* Upload button */
    plugin_data->upload_btn = gtk_button_new_with_label("Upload");
    gtk_widget_show(plugin_data->upload_btn);
    g_signal_connect(plugin_data->upload_btn, "clicked", G_CALLBACK(on_upload_clicked), plugin_data);
    gtk_box_pack_start(GTK_BOX(toolbar), plugin_data->upload_btn, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(browser_vbox), toolbar, FALSE, FALSE, 0);

    /* Editable path entry */
    plugin_data->path_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(plugin_data->path_entry), "/");
    gtk_widget_show(plugin_data->path_entry);
    g_signal_connect(plugin_data->path_entry, "activate",
                     G_CALLBACK(on_path_entry_activated), plugin_data);
    gtk_box_pack_start(GTK_BOX(browser_vbox), plugin_data->path_entry, FALSE, FALSE, 0);

    /* File list */
    scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_widget_show(scrolled_window);

    /* Create tree view with columns: Name(0), Type(1), Size(2), Icon(3), Modified(4), MTime(5-for sorting) */
    list_store = gtk_list_store_new(6, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT64);

    /* Create sortable model */
    GtkTreeSortable *sortable = GTK_TREE_SORTABLE(list_store);

    plugin_data->file_treeview = gtk_tree_view_new_with_model(GTK_TREE_MODEL(list_store));
    gtk_widget_show(plugin_data->file_treeview);

    /* Icon + Name column (sortable) */
    column = gtk_tree_view_column_new();
    gtk_tree_view_column_set_title(column, "Name");
    gtk_tree_view_column_set_sort_column_id(column, 0);
    gtk_tree_view_column_set_resizable(column, TRUE);

    GtkCellRenderer *icon_renderer = gtk_cell_renderer_pixbuf_new();
    gtk_tree_view_column_pack_start(column, icon_renderer, FALSE);
    gtk_tree_view_column_add_attribute(column, icon_renderer, "icon-name", 3);

    renderer = gtk_cell_renderer_text_new();
    gtk_tree_view_column_pack_start(column, renderer, TRUE);
    gtk_tree_view_column_add_attribute(column, renderer, "text", 0);

    gtk_tree_view_append_column(GTK_TREE_VIEW(plugin_data->file_treeview), column);

    /* Type column (sortable) */
    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes("Type", renderer, "text", 1, NULL);
    gtk_tree_view_column_set_sort_column_id(column, 1);
    gtk_tree_view_column_set_resizable(column, TRUE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(plugin_data->file_treeview), column);

    /* Size column (sortable) */
    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes("Size", renderer, "text", 2, NULL);
    gtk_tree_view_column_set_sort_column_id(column, 2);
    gtk_tree_view_column_set_resizable(column, TRUE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(plugin_data->file_treeview), column);

    /* Modified column (sortable by mtime) */
    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes("Modified", renderer, "text", 4, NULL);
    gtk_tree_view_column_set_sort_column_id(column, 5);  /* Sort by mtime (column 5) */
    gtk_tree_view_column_set_resizable(column, TRUE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(plugin_data->file_treeview), column);

    /* Set default sort by name */
    gtk_tree_sortable_set_sort_column_id(sortable, 0, GTK_SORT_ASCENDING);

    /* Connect double-click and right-click handlers */
    g_signal_connect(plugin_data->file_treeview, "row-activated",
                     G_CALLBACK(on_file_row_activated), plugin_data);
    g_signal_connect(plugin_data->file_treeview, "button-press-event",
                     G_CALLBACK(on_file_button_press), plugin_data);

    gtk_container_add(GTK_CONTAINER(scrolled_window), plugin_data->file_treeview);
    gtk_box_pack_start(GTK_BOX(browser_vbox), scrolled_window, TRUE, TRUE, 0);

    gtk_container_add(GTK_CONTAINER(browser_frame), browser_vbox);
    gtk_box_pack_start(GTK_BOX(sidebar_vbox), browser_frame, TRUE, TRUE, 0);

    /* Update connection list */
    update_connection_combo(plugin_data);

    plugin_data->sidebar = sidebar_vbox;
}

/*
 * Update file list
 */
void ui_update_file_list(SFTPPluginData *plugin_data)
{
    GtkListStore *list_store;
    GtkTreeIter iter;
    SFTPSession *session;
    LIBSSH2_SFTP_HANDLE *handle;
    LIBSSH2_SFTP_ATTRIBUTES attrs;
    char filename[256];
    int rc;

    if (plugin_data->current_connection < 0 ||
        !plugin_data->sessions[plugin_data->current_connection]) {
        return;
    }

    session = plugin_data->sessions[plugin_data->current_connection];

    list_store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(plugin_data->file_treeview)));
    gtk_list_store_clear(list_store);

    /* Add ".." entry for parent directory (if not at root) */
    if (strcmp(plugin_data->current_remote_path, "/") != 0) {
        gtk_list_store_append(list_store, &iter);
        gtk_list_store_set(list_store, &iter, 0, "..", 1, "DIR", 2, "", 3, "folder", 4, "", 5, (gint64)0, -1);
    }

    /* Open directory */
    handle = libssh2_sftp_opendir(session->sftp_session, plugin_data->current_remote_path);
    if (!handle) {
        g_printerr("Cannot open directory: %s\n", plugin_data->current_remote_path);
        return;
    }

    /* Read directory contents */
    while ((rc = libssh2_sftp_readdir(handle, filename, sizeof(filename), &attrs)) > 0) {
        const gchar *type;
        const gchar *icon;
        gchar size_str[32];
        gchar mtime_str[32];
        gint64 mtime = 0;

        /* Skip "." and ".." entries - we add ".." manually above */
        if (strcmp(filename, ".") == 0 || strcmp(filename, "..") == 0) {
            continue;
        }

        if (filename[0] == '.' && !plugin_data->show_hidden_files) {
            continue; /* Skip hidden files */
        }

        /* Get modification time */
        if (attrs.flags & LIBSSH2_SFTP_ATTR_ACMODTIME) {
            mtime = (gint64)attrs.mtime;
            struct tm *tm_info = localtime((time_t *)&attrs.mtime);
            strftime(mtime_str, sizeof(mtime_str), "%Y-%m-%d %H:%M", tm_info);
        } else {
            strcpy(mtime_str, "");
        }

        /* Determine file type and icon */
        if (attrs.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS &&
            (attrs.permissions & LIBSSH2_SFTP_S_IFDIR)) {
            type = "DIR";
            icon = "folder";
            strcpy(size_str, "");
        } else {
            type = "FILE";
            icon = "text-x-generic";
            g_snprintf(size_str, sizeof(size_str), "%ld", (long)attrs.filesize);
        }

        /* Add to list */
        gtk_list_store_append(list_store, &iter);
        gtk_list_store_set(list_store, &iter,
                          0, filename,
                          1, type,
                          2, size_str,
                          3, icon,
                          4, mtime_str,
                          5, mtime,
                          -1);
    }

    libssh2_sftp_closedir(handle);

    /* Update path entry */
    gtk_entry_set_text(GTK_ENTRY(plugin_data->path_entry),
                      plugin_data->current_remote_path);
}

/*
 * Progress dialog context
 */
typedef struct {
    GtkWidget *dialog;
    GtkWidget *progress_bar;
    GtkWidget *label;
    FileOperation *op;
    guint timer_id;
} ProgressCtx;

/* Timer callback: update progress bar from worker thread's progress */
static gboolean progress_timer_cb(gpointer data)
{
    ProgressCtx *ctx = (ProgressCtx *)data;
    FileOperation *op = ctx->op;

    if (op->completed || op->cancelled) {
        gtk_widget_destroy(ctx->dialog);
        g_free(ctx);
        return G_SOURCE_REMOVE;
    }

    gsize transferred = (gsize)g_atomic_pointer_get(&op->transferred);
    gsize total = op->total_size;

    if (total > 0) {
        gdouble fraction = (gdouble)transferred / (gdouble)total;
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(ctx->progress_bar), fraction);

        gchar *text;
        if (total >= 1048576)
            text = g_strdup_printf("%.1f / %.1f MB", transferred / 1048576.0, total / 1048576.0);
        else
            text = g_strdup_printf("%.1f / %.1f KB", transferred / 1024.0, total / 1024.0);
        gtk_label_set_text(GTK_LABEL(ctx->label), text);
        g_free(text);
    } else {
        gtk_progress_bar_pulse(GTK_PROGRESS_BAR(ctx->progress_bar));
    }

    return G_SOURCE_CONTINUE;
}

/* Cancel button handler */
static void on_progress_cancel(GtkDialog *dialog, gint response_id, gpointer data)
{
    ProgressCtx *ctx = (ProgressCtx *)data;
    (void)dialog;
    if (response_id == GTK_RESPONSE_CANCEL)
        ctx->op->cancelled = TRUE;
}

/*
 * Show non-modal progress dialog that auto-updates from FileOperation
 */
void ui_show_progress_dialog(SFTPPluginData *plugin_data, FileOperation *op)
{
    (void)plugin_data;

    ProgressCtx *ctx = g_new0(ProgressCtx, 1);
    ctx->op = op;

    gchar *title = g_strdup_printf("%s: %s",
        op->is_upload ? "Uploading" : "Downloading",
        g_path_get_basename(op->is_upload ? op->local_path : op->remote_path));

    ctx->dialog = gtk_dialog_new_with_buttons(title, NULL, 0,
                                              "_Cancel", GTK_RESPONSE_CANCEL, NULL);
    g_free(title);
    gtk_window_set_default_size(GTK_WINDOW(ctx->dialog), 350, 80);

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(ctx->dialog));

    ctx->progress_bar = gtk_progress_bar_new();
    gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(ctx->progress_bar), TRUE);
    gtk_widget_show(ctx->progress_bar);
    gtk_box_pack_start(GTK_BOX(content), ctx->progress_bar, FALSE, FALSE, 10);

    ctx->label = gtk_label_new("Starting...");
    gtk_widget_show(ctx->label);
    gtk_box_pack_start(GTK_BOX(content), ctx->label, FALSE, FALSE, 5);

    g_signal_connect(ctx->dialog, "response", G_CALLBACK(on_progress_cancel), ctx);
    gtk_widget_show(ctx->dialog);

    /* Poll progress every 100ms */
    ctx->timer_id = g_timeout_add(100, progress_timer_cb, ctx);
}
