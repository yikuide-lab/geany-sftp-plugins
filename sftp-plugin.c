/*
 * Geany SFTP Plugin - Main plugin file
 * Implemented in C with libssh2
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "sftp-plugin.h"
#include "compat.h"

#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

GeanyData *geany_data;
GeanyPlugin *geany_plugin;

static SFTPPluginData *plugin_data = NULL;

/* Function declarations */
static gboolean sftp_plugin_init(GeanyPlugin *plugin, gpointer pdata);
static void sftp_plugin_cleanup(GeanyPlugin *plugin, gpointer pdata);
static GtkWidget *sftp_configure(GeanyPlugin *plugin, GtkDialog *dialog, gpointer pdata);
static void sftp_help(GeanyPlugin *plugin, gpointer pdata);
static void on_document_save(GObject *obj, GeanyDocument *doc, gpointer user_data);

/* Forward declaration for sidebar update */
void ui_update_connection_combo(SFTPPluginData *plugin_data);

/*
 * Plugin init function
 */
static gboolean sftp_plugin_init(GeanyPlugin *plugin, gpointer pdata)
{
    (void)pdata;
    geany_plugin = plugin;
    geany_data = plugin->geany_data;

    /* Initialize libssh2 */
    compat_winsock_init();
    if (libssh2_init(0) != 0) {
        g_printerr("libssh2 init failed\n");
        return FALSE;
    }

    /* Allocate plugin data */
    plugin_data = g_try_new0(SFTPPluginData, 1);
    if (!plugin_data) {
        g_printerr("Failed to allocate plugin data\n");
        return FALSE;
    }

    plugin_data->geany_plugin = plugin;
    plugin_data->geany_data = geany_data;
    plugin_data->num_connections = 0;
    plugin_data->current_connection = -1;
    plugin_data->active_operations = NULL;
    plugin_data->completed_operations = NULL;
    plugin_data->downloaded_files = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    strcpy(plugin_data->current_remote_path, ".");

    /* Load config */
    config_load_settings(plugin_data);
    config_load_connections(plugin_data);

    /* Create UI */
    ui_create_sidebar(plugin_data);

    /* Add sidebar to Geany's sidebar notebook */
    if (plugin_data->sidebar) {
        GtkWidget *sidebar_label = gtk_label_new("SFTP");
        gtk_notebook_append_page(GTK_NOTEBOOK(geany_data->main_widgets->sidebar_notebook),
                                 plugin_data->sidebar, sidebar_label);
        gtk_widget_show_all(plugin_data->sidebar);
    }

    /* Connect to document-save signal for auto-upload */
    plugin_signal_connect(plugin, NULL, "document-save", TRUE,
                          G_CALLBACK(on_document_save), plugin_data);

    g_print("SFTP Plugin loaded\n");
    return TRUE;
}

/*
 * Document save callback - auto upload if configured
 */
static void on_document_save(GObject *obj, GeanyDocument *doc, gpointer user_data)
{
    SFTPPluginData *pdata = (SFTPPluginData *)user_data;
    const gchar *remote_path;
    SFTPSession *session;

    (void)obj;

    if (!pdata->auto_upload || !doc || !doc->file_name)
        return;

    /* Check if connected */
    if (pdata->current_connection < 0 ||
        !pdata->sessions[pdata->current_connection] ||
        !pdata->sessions[pdata->current_connection]->active)
        return;

    /* Check if this file was downloaded from SFTP */
    remote_path = g_hash_table_lookup(pdata->downloaded_files, doc->file_name);
    if (!remote_path)
        return;

    session = pdata->sessions[pdata->current_connection];

    /* Upload the file asynchronously */
    transfer_async(session, doc->file_name, remote_path, TRUE, NULL, NULL);
    g_print("Auto-upload started: %s -> %s\n", doc->file_name, remote_path);
}

/*
 * Plugin cleanup function
 */
static void sftp_plugin_cleanup(GeanyPlugin *plugin, gpointer pdata)
{
    (void)plugin;
    (void)pdata;
    gint i;

    if (!plugin_data)
        return;

    /* Close all connections */
    for (i = 0; i < MAX_CONNECTIONS; i++) {
        if (plugin_data->sessions[i]) {
            sftp_connection_disconnect(plugin_data->sessions[i]);
            g_mutex_clear(&plugin_data->sessions[i]->lock);
            g_free(plugin_data->sessions[i]);
            plugin_data->sessions[i] = NULL;
        }
    }

    /* Cleanup file operations */
    g_list_free_full(plugin_data->active_operations, g_free);
    g_list_free_full(plugin_data->completed_operations, g_free);

    /* Cleanup downloaded files hash table */
    if (plugin_data->downloaded_files)
        g_hash_table_destroy(plugin_data->downloaded_files);

    /* Cleanup libssh2 */
    libssh2_exit();
    compat_winsock_cleanup();

    /* Cleanup UI */
    if (plugin_data->menu_item) {
        gtk_widget_destroy(plugin_data->menu_item);
    }

    /* Save config */
    config_save_connections(plugin_data);
    config_save_settings(plugin_data);

    g_free(plugin_data);
    plugin_data = NULL;

    g_print("SFTP Plugin unloaded\n");
}

/* Helper function to refresh config dialog connection list */
static void refresh_config_conn_list(void)
{
    if (!plugin_data->config_conn_list)
        return;

    GtkListStore *list_store = GTK_LIST_STORE(
        gtk_tree_view_get_model(GTK_TREE_VIEW(plugin_data->config_conn_list)));
    GtkTreeIter iter;
    int i;

    gtk_list_store_clear(list_store);
    for (i = 0; i < plugin_data->num_connections; i++) {
        gtk_list_store_append(list_store, &iter);
        gtk_list_store_set(list_store, &iter,
                          0, plugin_data->connections[i].name,
                          1, plugin_data->connections[i].hostname,
                          2, plugin_data->connections[i].port,
                          -1);
    }
}

/* Get selected connection index from config dialog */
static gint get_selected_connection_index(void)
{
    GtkTreeSelection *selection;
    GtkTreeModel *model;
    GtkTreeIter iter;
    GtkTreePath *path;
    gint *indices;
    gint index = -1;

    if (!plugin_data->config_conn_list)
        return -1;

    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(plugin_data->config_conn_list));
    if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
        path = gtk_tree_model_get_path(model, &iter);
        indices = gtk_tree_path_get_indices(path);
        if (indices)
            index = indices[0];
        gtk_tree_path_free(path);
    }
    return index;
}

/* Data structure for SSH host selection callback */
typedef struct {
    GtkWidget *host_entry;
    GtkWidget *port_entry;
    GtkWidget *user_entry;
    GtkWidget *key_entry;
    GtkWidget *name_entry;
    SSHConfigHost *ssh_hosts;
    gint num_hosts;
} SSHHostSelectData;

/* Callback for SSH host selection */
static void on_ssh_host_selected(GtkComboBox *combo, gpointer data)
{
    SSHHostSelectData *select_data = (SSHHostSelectData *)data;
    gint index = gtk_combo_box_get_active(combo);
    gchar port_str[16];

    if (index <= 0 || index > select_data->num_hosts)
        return;

    SSHConfigHost *host = &select_data->ssh_hosts[index - 1];

    /* Fill in the form fields */
    if (host->hostname[0])
        gtk_entry_set_text(GTK_ENTRY(select_data->host_entry), host->hostname);
    else
        gtk_entry_set_text(GTK_ENTRY(select_data->host_entry), host->name);

    snprintf(port_str, sizeof(port_str), "%d", host->port);
    gtk_entry_set_text(GTK_ENTRY(select_data->port_entry), port_str);

    if (host->username[0])
        gtk_entry_set_text(GTK_ENTRY(select_data->user_entry), host->username);

    if (host->identity_file[0])
        gtk_entry_set_text(GTK_ENTRY(select_data->key_entry), host->identity_file);

    if (select_data->name_entry && host->name[0])
        gtk_entry_set_text(GTK_ENTRY(select_data->name_entry), host->name);
}

/* Browse for SSH key file */
static void on_key_browse_clicked(GtkButton *button, gpointer data)
{
    GtkWidget *dialog;
    GtkWidget *key_entry = GTK_WIDGET(data);
    gchar *filename;
    (void)button;

    dialog = gtk_file_chooser_dialog_new("Select Private Key",
                                          NULL, GTK_FILE_CHOOSER_ACTION_OPEN,
                                          "_Cancel", GTK_RESPONSE_CANCEL,
                                          "_Open", GTK_RESPONSE_ACCEPT, NULL);

    /* Start in ~/.ssh directory */
    gchar *ssh_dir = g_build_filename(g_get_home_dir(), ".ssh", NULL);
    gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog), ssh_dir);
    g_free(ssh_dir);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        gtk_entry_set_text(GTK_ENTRY(key_entry), filename);
        g_free(filename);
    }
    gtk_widget_destroy(dialog);
}

/* Data structure for test connection callback */
typedef struct {
    GtkWidget *host_entry;
    GtkWidget *port_entry;
    GtkWidget *user_entry;
    GtkWidget *pass_entry;
    GtkWidget *key_entry;
} TestConnData;

/* Test connection callback */
static void on_test_connection_clicked(GtkButton *button, gpointer data)
{
    TestConnData *test_data = (TestConnData *)data;
    SFTPConnection test_conn = {0};
    SFTPSession test_session = {0};
    (void)button;

    /* Get values from dialog */
    strncpy(test_conn.hostname, gtk_entry_get_text(GTK_ENTRY(test_data->host_entry)), sizeof(test_conn.hostname) - 1);
    test_conn.port = atoi(gtk_entry_get_text(GTK_ENTRY(test_data->port_entry)));
    strncpy(test_conn.username, gtk_entry_get_text(GTK_ENTRY(test_data->user_entry)), sizeof(test_conn.username) - 1);
    strncpy(test_conn.password, gtk_entry_get_text(GTK_ENTRY(test_data->pass_entry)), sizeof(test_conn.password) - 1);
    strncpy(test_conn.private_key, gtk_entry_get_text(GTK_ENTRY(test_data->key_entry)), sizeof(test_conn.private_key) - 1);

    /* Validate required fields */
    if (!test_conn.hostname[0] || !test_conn.username[0]) {
        dialogs_show_msgbox(GTK_MESSAGE_WARNING, "Please fill in Host and Username");
        return;
    }

    test_session.config = &test_conn;

    /* Try to connect */
    if (sftp_connection_connect(&test_session)) {
        dialogs_show_msgbox(GTK_MESSAGE_INFO, "Connection successful!");
        sftp_connection_disconnect(&test_session);
    } else {
        dialogs_show_msgbox(GTK_MESSAGE_ERROR, "Connection failed!\nPlease check your settings.");
    }
}

/*
 * Edit connection dialog
 */
static void show_edit_connection_dialog(GtkWidget *parent, gint conn_index)
{
    GtkWidget *dialog, *content, *grid;
    GtkWidget *name_entry, *host_entry, *port_entry, *user_entry, *pass_entry, *dir_entry;
    GtkWidget *auth_combo, *key_entry, *key_browse_btn, *key_box;
    GtkWidget *ssh_host_combo, *test_btn;
    GtkWidget *label;
    gint response;
    SFTPConnection *conn;
    gchar port_str[16];
    SSHConfigHost ssh_hosts[MAX_SSH_HOSTS];
    gint num_ssh_hosts;
    SSHHostSelectData *select_data;
    TestConnData *test_data;
    gint i;

    if (conn_index < 0 || conn_index >= plugin_data->num_connections)
        return;

    conn = &plugin_data->connections[conn_index];

    dialog = gtk_dialog_new_with_buttons("Edit Connection", GTK_WINDOW(gtk_widget_get_toplevel(parent)),
                                         GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                         "_Cancel", GTK_RESPONSE_CANCEL,
                                         "_OK", GTK_RESPONSE_OK, NULL);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 450, -1);

    content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 5);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 10);

    /* SSH Config Host selector */
    label = gtk_label_new("SSH Config:"); gtk_grid_attach(GTK_GRID(grid), label, 0, 0, 1, 1);
    ssh_host_combo = gtk_combo_box_text_new();
    gtk_widget_set_hexpand(ssh_host_combo, TRUE);
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(ssh_host_combo), "-- Select from ~/.ssh/config --");

    /* Load SSH hosts */
    num_ssh_hosts = config_load_ssh_hosts(ssh_hosts, MAX_SSH_HOSTS);
    for (i = 0; i < num_ssh_hosts; i++) {
        gchar *display = g_strdup_printf("%s (%s)", ssh_hosts[i].name,
            ssh_hosts[i].hostname[0] ? ssh_hosts[i].hostname : ssh_hosts[i].name);
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(ssh_host_combo), display);
        g_free(display);
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(ssh_host_combo), 0);
    gtk_grid_attach(GTK_GRID(grid), ssh_host_combo, 1, 0, 2, 1);

    label = gtk_label_new("Name:"); gtk_grid_attach(GTK_GRID(grid), label, 0, 1, 1, 1);
    name_entry = gtk_entry_new();
    gtk_widget_set_hexpand(name_entry, TRUE);
    gtk_entry_set_text(GTK_ENTRY(name_entry), conn->name);
    gtk_grid_attach(GTK_GRID(grid), name_entry, 1, 1, 2, 1);

    label = gtk_label_new("Host:"); gtk_grid_attach(GTK_GRID(grid), label, 0, 2, 1, 1);
    host_entry = gtk_entry_new();
    gtk_widget_set_hexpand(host_entry, TRUE);
    gtk_entry_set_text(GTK_ENTRY(host_entry), conn->hostname);
    gtk_grid_attach(GTK_GRID(grid), host_entry, 1, 2, 2, 1);

    label = gtk_label_new("Port:"); gtk_grid_attach(GTK_GRID(grid), label, 0, 3, 1, 1);
    port_entry = gtk_entry_new();
    snprintf(port_str, sizeof(port_str), "%d", conn->port);
    gtk_entry_set_text(GTK_ENTRY(port_entry), port_str);
    gtk_grid_attach(GTK_GRID(grid), port_entry, 1, 3, 2, 1);

    label = gtk_label_new("Username:"); gtk_grid_attach(GTK_GRID(grid), label, 0, 4, 1, 1);
    user_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(user_entry), conn->username);
    gtk_grid_attach(GTK_GRID(grid), user_entry, 1, 4, 2, 1);

    label = gtk_label_new("Auth Method:"); gtk_grid_attach(GTK_GRID(grid), label, 0, 5, 1, 1);
    auth_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(auth_combo), "Password");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(auth_combo), "SSH Key");
    gtk_combo_box_set_active(GTK_COMBO_BOX(auth_combo), conn->private_key[0] ? 1 : 0);
    gtk_grid_attach(GTK_GRID(grid), auth_combo, 1, 5, 2, 1);

    label = gtk_label_new("Password:"); gtk_grid_attach(GTK_GRID(grid), label, 0, 6, 1, 1);
    pass_entry = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(pass_entry), FALSE);
    gtk_entry_set_text(GTK_ENTRY(pass_entry), conn->password);
    gtk_grid_attach(GTK_GRID(grid), pass_entry, 1, 6, 2, 1);

    label = gtk_label_new("Private Key:"); gtk_grid_attach(GTK_GRID(grid), label, 0, 7, 1, 1);
    key_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    key_entry = gtk_entry_new();
    gtk_widget_set_hexpand(key_entry, TRUE);
    gtk_entry_set_placeholder_text(GTK_ENTRY(key_entry), "~/.ssh/id_rsa");
    gtk_entry_set_text(GTK_ENTRY(key_entry), conn->private_key);
    gtk_box_pack_start(GTK_BOX(key_box), key_entry, TRUE, TRUE, 0);
    key_browse_btn = gtk_button_new_with_label("Browse...");
    gtk_box_pack_start(GTK_BOX(key_box), key_browse_btn, FALSE, FALSE, 0);
    gtk_grid_attach(GTK_GRID(grid), key_box, 1, 7, 2, 1);

    label = gtk_label_new("Remote Dir:"); gtk_grid_attach(GTK_GRID(grid), label, 0, 8, 1, 1);
    dir_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(dir_entry), conn->remote_dir);
    gtk_grid_attach(GTK_GRID(grid), dir_entry, 1, 8, 2, 1);

    /* Test Connection button */
    test_btn = gtk_button_new_with_label("Test Connection");
    gtk_grid_attach(GTK_GRID(grid), test_btn, 1, 9, 2, 1);

    /* Setup SSH host selection callback */
    select_data = g_new0(SSHHostSelectData, 1);
    select_data->host_entry = host_entry;
    select_data->port_entry = port_entry;
    select_data->user_entry = user_entry;
    select_data->key_entry = key_entry;
    select_data->name_entry = NULL;  /* Don't auto-fill name when editing */
    select_data->ssh_hosts = ssh_hosts;
    select_data->num_hosts = num_ssh_hosts;
    g_signal_connect(ssh_host_combo, "changed", G_CALLBACK(on_ssh_host_selected), select_data);

    /* Test connection callback */
    test_data = g_new0(TestConnData, 1);
    test_data->host_entry = host_entry;
    test_data->port_entry = port_entry;
    test_data->user_entry = user_entry;
    test_data->pass_entry = pass_entry;
    test_data->key_entry = key_entry;
    g_signal_connect(test_btn, "clicked", G_CALLBACK(on_test_connection_clicked), test_data);

    /* Browse button callback */
    g_signal_connect(key_browse_btn, "clicked", G_CALLBACK(on_key_browse_clicked), key_entry);

    gtk_box_pack_start(GTK_BOX(content), grid, TRUE, TRUE, 0);
    gtk_widget_show_all(dialog);

    response = gtk_dialog_run(GTK_DIALOG(dialog));
    if (response == GTK_RESPONSE_OK) {
        strncpy(conn->name, gtk_entry_get_text(GTK_ENTRY(name_entry)), sizeof(conn->name) - 1);
        strncpy(conn->hostname, gtk_entry_get_text(GTK_ENTRY(host_entry)), sizeof(conn->hostname) - 1);
        conn->port = atoi(gtk_entry_get_text(GTK_ENTRY(port_entry)));
        strncpy(conn->username, gtk_entry_get_text(GTK_ENTRY(user_entry)), sizeof(conn->username) - 1);
        strncpy(conn->password, gtk_entry_get_text(GTK_ENTRY(pass_entry)), sizeof(conn->password) - 1);
        strncpy(conn->private_key, gtk_entry_get_text(GTK_ENTRY(key_entry)), sizeof(conn->private_key) - 1);
        strncpy(conn->remote_dir, gtk_entry_get_text(GTK_ENTRY(dir_entry)), sizeof(conn->remote_dir) - 1);

        config_save_connections(plugin_data);
        ui_update_connection_combo(plugin_data);
        refresh_config_conn_list();
        dialogs_show_msgbox(GTK_MESSAGE_INFO, "Connection updated");
    }

    g_free(select_data);
    g_free(test_data);
    gtk_widget_destroy(dialog);
}

/*
 * Delete connection
 */
static void delete_connection(gint conn_index)
{
    int i;

    if (conn_index < 0 || conn_index >= plugin_data->num_connections)
        return;

    /* Close session if connected */
    if (plugin_data->sessions[conn_index]) {
        sftp_connection_disconnect(plugin_data->sessions[conn_index]);
        g_free(plugin_data->sessions[conn_index]);
        plugin_data->sessions[conn_index] = NULL;
    }

    /* Shift connections */
    for (i = conn_index; i < plugin_data->num_connections - 1; i++) {
        plugin_data->connections[i] = plugin_data->connections[i + 1];
        plugin_data->sessions[i] = plugin_data->sessions[i + 1];
    }
    plugin_data->sessions[plugin_data->num_connections - 1] = NULL;
    plugin_data->num_connections--;

    /* Reset current connection */
    if (plugin_data->current_connection >= plugin_data->num_connections)
        plugin_data->current_connection = plugin_data->num_connections - 1;

    config_save_connections(plugin_data);
    ui_update_connection_combo(plugin_data);
    refresh_config_conn_list();
}

static void on_edit_connection_clicked(GtkButton *button, gpointer data)
{
    gint index;
    (void)data;

    index = get_selected_connection_index();
    if (index < 0) {
        dialogs_show_msgbox(GTK_MESSAGE_WARNING, "Please select a connection first");
        return;
    }
    show_edit_connection_dialog(GTK_WIDGET(button), index);
}

static void on_delete_connection_clicked(GtkButton *button, gpointer data)
{
    gint index;
    (void)button;
    (void)data;

    index = get_selected_connection_index();
    if (index < 0) {
        dialogs_show_msgbox(GTK_MESSAGE_WARNING, "Please select a connection first");
        return;
    }

    if (dialogs_show_question("Are you sure you want to delete this connection?")) {
        delete_connection(index);
        dialogs_show_msgbox(GTK_MESSAGE_INFO, "Connection deleted");
    }
}

/*
 * Add connection dialog
 */
static void show_add_connection_dialog(GtkWidget *parent)
{
    GtkWidget *dialog, *content, *grid;
    GtkWidget *name_entry, *host_entry, *port_entry, *user_entry, *pass_entry, *dir_entry;
    GtkWidget *auth_combo, *key_entry, *key_browse_btn, *key_box;
    GtkWidget *ssh_host_combo, *test_btn;
    GtkWidget *label;
    gint response;
    SSHConfigHost ssh_hosts[MAX_SSH_HOSTS];
    gint num_ssh_hosts;
    SSHHostSelectData *select_data;
    TestConnData *test_data;
    gint i;

    dialog = gtk_dialog_new_with_buttons("Add Connection", GTK_WINDOW(gtk_widget_get_toplevel(parent)),
                                         GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                         "_Cancel", GTK_RESPONSE_CANCEL,
                                         "_OK", GTK_RESPONSE_OK, NULL);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 450, -1);

    content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 5);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 10);

    /* SSH Config Host selector */
    label = gtk_label_new("SSH Config:"); gtk_grid_attach(GTK_GRID(grid), label, 0, 0, 1, 1);
    ssh_host_combo = gtk_combo_box_text_new();
    gtk_widget_set_hexpand(ssh_host_combo, TRUE);
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(ssh_host_combo), "-- Select from ~/.ssh/config --");

    /* Load SSH hosts */
    num_ssh_hosts = config_load_ssh_hosts(ssh_hosts, MAX_SSH_HOSTS);
    for (i = 0; i < num_ssh_hosts; i++) {
        gchar *display = g_strdup_printf("%s (%s)", ssh_hosts[i].name,
            ssh_hosts[i].hostname[0] ? ssh_hosts[i].hostname : ssh_hosts[i].name);
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(ssh_host_combo), display);
        g_free(display);
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(ssh_host_combo), 0);
    gtk_grid_attach(GTK_GRID(grid), ssh_host_combo, 1, 0, 2, 1);

    label = gtk_label_new("Name:"); gtk_grid_attach(GTK_GRID(grid), label, 0, 1, 1, 1);
    name_entry = gtk_entry_new();
    gtk_widget_set_hexpand(name_entry, TRUE);
    gtk_grid_attach(GTK_GRID(grid), name_entry, 1, 1, 2, 1);

    label = gtk_label_new("Host:"); gtk_grid_attach(GTK_GRID(grid), label, 0, 2, 1, 1);
    host_entry = gtk_entry_new();
    gtk_widget_set_hexpand(host_entry, TRUE);
    gtk_grid_attach(GTK_GRID(grid), host_entry, 1, 2, 2, 1);

    label = gtk_label_new("Port:"); gtk_grid_attach(GTK_GRID(grid), label, 0, 3, 1, 1);
    port_entry = gtk_entry_new(); gtk_entry_set_text(GTK_ENTRY(port_entry), "22");
    gtk_grid_attach(GTK_GRID(grid), port_entry, 1, 3, 2, 1);

    label = gtk_label_new("Username:"); gtk_grid_attach(GTK_GRID(grid), label, 0, 4, 1, 1);
    user_entry = gtk_entry_new();
    gtk_grid_attach(GTK_GRID(grid), user_entry, 1, 4, 2, 1);

    label = gtk_label_new("Auth Method:"); gtk_grid_attach(GTK_GRID(grid), label, 0, 5, 1, 1);
    auth_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(auth_combo), "Password");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(auth_combo), "SSH Key");
    gtk_combo_box_set_active(GTK_COMBO_BOX(auth_combo), 0);
    gtk_grid_attach(GTK_GRID(grid), auth_combo, 1, 5, 2, 1);

    label = gtk_label_new("Password:"); gtk_grid_attach(GTK_GRID(grid), label, 0, 6, 1, 1);
    pass_entry = gtk_entry_new(); gtk_entry_set_visibility(GTK_ENTRY(pass_entry), FALSE);
    gtk_grid_attach(GTK_GRID(grid), pass_entry, 1, 6, 2, 1);

    label = gtk_label_new("Private Key:"); gtk_grid_attach(GTK_GRID(grid), label, 0, 7, 1, 1);
    key_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    key_entry = gtk_entry_new();
    gtk_widget_set_hexpand(key_entry, TRUE);
    gtk_entry_set_placeholder_text(GTK_ENTRY(key_entry), "~/.ssh/id_rsa");
    gtk_box_pack_start(GTK_BOX(key_box), key_entry, TRUE, TRUE, 0);
    key_browse_btn = gtk_button_new_with_label("Browse...");
    gtk_box_pack_start(GTK_BOX(key_box), key_browse_btn, FALSE, FALSE, 0);
    gtk_grid_attach(GTK_GRID(grid), key_box, 1, 7, 2, 1);

    label = gtk_label_new("Remote Dir:"); gtk_grid_attach(GTK_GRID(grid), label, 0, 8, 1, 1);
    dir_entry = gtk_entry_new(); gtk_entry_set_text(GTK_ENTRY(dir_entry), "/");
    gtk_grid_attach(GTK_GRID(grid), dir_entry, 1, 8, 2, 1);

    /* Test Connection button */
    test_btn = gtk_button_new_with_label("Test Connection");
    gtk_grid_attach(GTK_GRID(grid), test_btn, 1, 9, 2, 1);

    /* Setup SSH host selection callback */
    select_data = g_new0(SSHHostSelectData, 1);
    select_data->host_entry = host_entry;
    select_data->port_entry = port_entry;
    select_data->user_entry = user_entry;
    select_data->key_entry = key_entry;
    select_data->name_entry = name_entry;
    select_data->ssh_hosts = ssh_hosts;
    select_data->num_hosts = num_ssh_hosts;
    g_signal_connect(ssh_host_combo, "changed", G_CALLBACK(on_ssh_host_selected), select_data);

    /* Test connection callback */
    test_data = g_new0(TestConnData, 1);
    test_data->host_entry = host_entry;
    test_data->port_entry = port_entry;
    test_data->user_entry = user_entry;
    test_data->pass_entry = pass_entry;
    test_data->key_entry = key_entry;
    g_signal_connect(test_btn, "clicked", G_CALLBACK(on_test_connection_clicked), test_data);

    /* Browse button callback */
    g_signal_connect(key_browse_btn, "clicked", G_CALLBACK(on_key_browse_clicked), key_entry);

    gtk_box_pack_start(GTK_BOX(content), grid, TRUE, TRUE, 0);
    gtk_widget_show_all(dialog);

    response = gtk_dialog_run(GTK_DIALOG(dialog));
    if (response == GTK_RESPONSE_OK && plugin_data->num_connections < MAX_CONNECTIONS) {
        SFTPConnection *conn = &plugin_data->connections[plugin_data->num_connections];
        strncpy(conn->name, gtk_entry_get_text(GTK_ENTRY(name_entry)), sizeof(conn->name) - 1);
        strncpy(conn->hostname, gtk_entry_get_text(GTK_ENTRY(host_entry)), sizeof(conn->hostname) - 1);
        conn->port = atoi(gtk_entry_get_text(GTK_ENTRY(port_entry)));
        strncpy(conn->username, gtk_entry_get_text(GTK_ENTRY(user_entry)), sizeof(conn->username) - 1);
        strncpy(conn->password, gtk_entry_get_text(GTK_ENTRY(pass_entry)), sizeof(conn->password) - 1);
        strncpy(conn->private_key, gtk_entry_get_text(GTK_ENTRY(key_entry)), sizeof(conn->private_key) - 1);
        strncpy(conn->remote_dir, gtk_entry_get_text(GTK_ENTRY(dir_entry)), sizeof(conn->remote_dir) - 1);
        conn->state = CONN_DISCONNECTED;
        plugin_data->num_connections++;
        config_save_connections(plugin_data);
        ui_update_connection_combo(plugin_data);
        refresh_config_conn_list();
        dialogs_show_msgbox(GTK_MESSAGE_INFO, "Connection added");
    }

    g_free(select_data);
    g_free(test_data);
    gtk_widget_destroy(dialog);
}

static void on_add_connection_clicked(GtkButton *button, gpointer data)
{
    (void)data;
    show_add_connection_dialog(GTK_WIDGET(button));
}

static void on_auto_upload_toggled(GtkToggleButton *toggle, gpointer data)
{
    (void)data;
    plugin_data->auto_upload = gtk_toggle_button_get_active(toggle);
    config_save_settings(plugin_data);
}

/*
 * Configure dialog function
 */
static GtkWidget *sftp_configure(GeanyPlugin *plugin, GtkDialog *dialog, gpointer pdata)
{
    GtkWidget *notebook;
    GtkWidget *label;
    GtkWidget *scrolled_window;
    GtkWidget *conn_list;
    GtkListStore *list_store;
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *column;
    GtkTreeIter iter;
    int i;

    (void)plugin;
    (void)pdata;

    /* Set dialog size wider */
    gtk_window_set_default_size(GTK_WINDOW(dialog), 500, 400);

    notebook = gtk_notebook_new();
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))),
                       notebook, TRUE, TRUE, 0);

    /* Connections tab */
    label = gtk_label_new("Connections");
    GtkWidget *conn_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), conn_page, label);

    /* Connection list */
    scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(scrolled_window, -1, 150);

    list_store = gtk_list_store_new(3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT);
    conn_list = gtk_tree_view_new_with_model(GTK_TREE_MODEL(list_store));

    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes("Name", renderer, "text", 0, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(conn_list), column);

    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes("Host", renderer, "text", 1, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(conn_list), column);

    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes("Port", renderer, "text", 2, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(conn_list), column);

    /* Populate connection list */
    for (i = 0; i < plugin_data->num_connections; i++) {
        gtk_list_store_append(list_store, &iter);
        gtk_list_store_set(list_store, &iter,
                          0, plugin_data->connections[i].name,
                          1, plugin_data->connections[i].hostname,
                          2, plugin_data->connections[i].port,
                          -1);
    }

    gtk_container_add(GTK_CONTAINER(scrolled_window), conn_list);
    gtk_box_pack_start(GTK_BOX(conn_page), scrolled_window, TRUE, TRUE, 5);

    /* Save reference for updating */
    plugin_data->config_conn_list = conn_list;

    /* Button box for Add/Edit/Delete */
    GtkWidget *btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);

    /* Add connection button */
    GtkWidget *add_btn = gtk_button_new_with_label("Add");
    g_signal_connect(add_btn, "clicked", G_CALLBACK(on_add_connection_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(btn_box), add_btn, TRUE, TRUE, 0);

    /* Edit connection button */
    GtkWidget *edit_btn = gtk_button_new_with_label("Edit");
    g_signal_connect(edit_btn, "clicked", G_CALLBACK(on_edit_connection_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(btn_box), edit_btn, TRUE, TRUE, 0);

    /* Delete connection button */
    GtkWidget *delete_btn = gtk_button_new_with_label("Delete");
    g_signal_connect(delete_btn, "clicked", G_CALLBACK(on_delete_connection_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(btn_box), delete_btn, TRUE, TRUE, 0);

    gtk_box_pack_start(GTK_BOX(conn_page), btn_box, FALSE, FALSE, 5);

    /* Settings tab */
    label = gtk_label_new("Settings");
    GtkWidget *settings_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), settings_page, label);

    /* Auto upload option */
    GtkWidget *auto_upload_check = gtk_check_button_new_with_label("Auto upload on save");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(auto_upload_check),
                                  plugin_data->auto_upload);
    g_signal_connect(auto_upload_check, "toggled", G_CALLBACK(on_auto_upload_toggled), NULL);
    gtk_box_pack_start(GTK_BOX(settings_page), auto_upload_check, FALSE, FALSE, 5);

    /* Show hidden files option */
    GtkWidget *show_hidden_check = gtk_check_button_new_with_label("Show hidden files");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(show_hidden_check),
                                  plugin_data->show_hidden_files);
    gtk_box_pack_start(GTK_BOX(settings_page), show_hidden_check, FALSE, FALSE, 5);

    gtk_widget_show_all(notebook);

    return notebook;
}

/*
 * Help dialog
 */
static void sftp_help(GeanyPlugin *plugin, gpointer pdata)
{
    (void)plugin;
    (void)pdata;

    dialogs_show_msgbox(GTK_MESSAGE_INFO,
        "Geany SFTP Plugin v%s\n\n"
        "Features:\n"
        "- Manage multiple SFTP connections\n"
        "- Browse remote file system\n"
        "- Upload and download files\n"
        "- File comparison and sync\n\n"
        "Usage:\n"
        "1. Connect to server from sidebar\n"
        "2. Browse remote files\n"
        "3. Double-click to download, right-click to upload\n\n"
        "Config location:\n"
        "~/.config/geany/plugins/sftp/",
         SFTP_PLUGIN_VERSION);
}

/*
 * Plugin load function - called by Geany
 */
G_MODULE_EXPORT
void geany_load_module(GeanyPlugin *plugin)
{
    /* Set plugin metadata */
    plugin->info->name = "SFTP Client";
    plugin->info->description = "SSH File Transfer Protocol client for remote file management";
    plugin->info->version = SFTP_PLUGIN_VERSION;
    plugin->info->author = "Developer <dev@example.com>";

    /* Set plugin functions */
    plugin->funcs->init = sftp_plugin_init;
    plugin->funcs->cleanup = sftp_plugin_cleanup;
    plugin->funcs->configure = sftp_configure;
    plugin->funcs->help = sftp_help;
    plugin->funcs->callbacks = NULL;

    /* Register plugin */
    GEANY_PLUGIN_REGISTER(plugin, 225);
}
