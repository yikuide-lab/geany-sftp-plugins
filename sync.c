/*
 * Sync Management Module
 * Implement file comparison and sync functionality
 */

#include "sftp-plugin.h"
#include "compat.h"

#include <sys/stat.h>

/*
 * Compare modification time of two files
 */
static gint compare_file_time(time_t t1, time_t t2)
{
    if (t1 < t2) return -1;
    if (t1 > t2) return 1;
    return 0;
}

/*
 * Download remote file to temp location for comparison
 */
static gboolean download_remote_file(SFTPSession *session, const gchar *remote_path,
                                     gchar *local_temp_path)
{
    LIBSSH2_SFTP_HANDLE *sftp_handle;
    FILE *local_file;
    char buf[8192];
    int rc;

    /* Create temp file path */
    g_snprintf(local_temp_path, MAX_PATH_LEN, "%s%ssftp_compare_XXXXXX",
               g_get_tmp_dir(), G_DIR_SEPARATOR_S);
    int fd = g_mkstemp(local_temp_path);
    if (fd < 0) {
        g_printerr("Failed to create temp file\n");
        return FALSE;
    }
    compat_close_fd(fd);

    /* Open remote file */
    sftp_handle = libssh2_sftp_open(session->sftp_session, remote_path,
                                    LIBSSH2_FXF_READ, 0);
    if (!sftp_handle) {
        g_printerr("Cannot open remote file: %s\n", remote_path);
        remove(local_temp_path);
        return FALSE;
    }

    /* Open local temp file */
    local_file = fopen(local_temp_path, "wb");
    if (!local_file) {
        g_printerr("Cannot open temp file\n");
        libssh2_sftp_close(sftp_handle);
        remove(local_temp_path);
        return FALSE;
    }

    /* Download file */
    while ((rc = libssh2_sftp_read(sftp_handle, buf, sizeof(buf))) > 0) {
        fwrite(buf, 1, rc, local_file);
    }

    libssh2_sftp_close(sftp_handle);
    fclose(local_file);

    return TRUE;
}

/*
 * Use external diff tool to compare files
 */
static gboolean run_external_diff(const gchar *local, const gchar *remote_temp)
{
    const gchar *diff_tools[] = {"meld", "diff", "kdiff3", NULL};
    gchar *cmd;
    int i;
    int result;

    for (i = 0; diff_tools[i] != NULL; i++) {
        /* Check if tool exists */
        if (system(g_strdup_printf("which %s > /dev/null 2>&1", diff_tools[i])) == 0) {
            /* Run diff tool */
            cmd = g_strdup_printf("%s %s %s", diff_tools[i], local, remote_temp);
            g_print("Running diff tool: %s\n", cmd);
            result = system(cmd);
            g_free(cmd);
            remove(remote_temp);
            return (result == 0);
        }
    }

    g_printerr("No external diff tool found\n");
    return FALSE;
}

/*
 * Compare local and remote files
 */
gboolean sync_compare_files(SFTPPluginData *plugin_data, const gchar *local,
                             const gchar *remote)
{
    struct stat local_stat;
    LIBSSH2_SFTP_ATTRIBUTES remote_stat;
    SFTPSession *session;
    gchar remote_temp[MAX_PATH_LEN];
    gboolean result;

    if (plugin_data->current_connection < 0 ||
        !plugin_data->sessions[plugin_data->current_connection]) {
        g_printerr("Not connected to server\n");
        return FALSE;
    }

    session = plugin_data->sessions[plugin_data->current_connection];

    /* Get local file info */
    if (stat(local, &local_stat) != 0) {
        g_printerr("Cannot get local file info: %s\n", local);
        return FALSE;
    }

    /* Get remote file info */
    if (libssh2_sftp_stat(session->sftp_session, remote, &remote_stat) != 0) {
        g_printerr("Cannot get remote file info: %s\n", remote);
        return FALSE;
    }

    g_print("Local file size: %ld, mtime: %ld\n", (long)local_stat.st_size,
           (long)local_stat.st_mtime);
    g_print("Remote file size: %ld, mtime: %ld\n", (long)remote_stat.filesize,
           (long)remote_stat.mtime);

    /* Download remote file to temp location */
    if (!download_remote_file(session, remote, remote_temp)) {
        return FALSE;
    }

    /* Use external diff tool to compare */
    result = run_external_diff(local, remote_temp);

    if (!result) {
        /* If external tool fails, show simple comparison result */
        gint time_cmp = compare_file_time(local_stat.st_mtime, remote_stat.mtime);

        if (time_cmp < 0) {
            dialogs_show_msgbox(GTK_MESSAGE_INFO,
                "Local file is older\n"
                "Remote file is newer\n"
                "Suggest downloading remote file");
        } else if (time_cmp > 0) {
            dialogs_show_msgbox(GTK_MESSAGE_INFO,
                "Local file is newer\n"
                "Remote file is older\n"
                "Suggest uploading local file");
        } else {
            dialogs_show_msgbox(GTK_MESSAGE_INFO,
                "File modification times are the same\n"
                "Files may be identical");
        }
    }

    return TRUE;
}

/*
 * Upload file (sync operation)
 */
gboolean sync_upload_file(SFTPPluginData *plugin_data, const gchar *local,
                          const gchar *remote)
{
    SFTPSession *session;

    if (plugin_data->current_connection < 0 ||
        !plugin_data->sessions[plugin_data->current_connection]) {
        g_printerr("Not connected to server\n");
        return FALSE;
    }

    session = plugin_data->sessions[plugin_data->current_connection];

    g_print("Sync upload: %s -> %s\n", local, remote);

    if (sftp_upload_file(session, local, remote, NULL)) {
        dialogs_show_msgbox(GTK_MESSAGE_INFO, "Upload successful");
        return TRUE;
    } else {
        dialogs_show_msgbox(GTK_MESSAGE_ERROR, "Upload failed");
        return FALSE;
    }
}

/*
 * Download file (sync operation)
 */
gboolean sync_download_file(SFTPPluginData *plugin_data, const gchar *remote,
                            const gchar *local)
{
    SFTPSession *session;

    if (plugin_data->current_connection < 0 ||
        !plugin_data->sessions[plugin_data->current_connection]) {
        g_printerr("Not connected to server\n");
        return FALSE;
    }

    session = plugin_data->sessions[plugin_data->current_connection];

    g_print("Sync download: %s -> %s\n", remote, local);

    if (sftp_download_file(session, remote, local, NULL)) {
        dialogs_show_msgbox(GTK_MESSAGE_INFO, "Download successful");
        return TRUE;
    } else {
        dialogs_show_msgbox(GTK_MESSAGE_ERROR, "Download failed");
        return FALSE;
    }
}

/*
 * Smart sync: determine sync direction based on file modification time
 */
gboolean sync_auto_sync(SFTPPluginData *plugin_data, const gchar *local,
                        const gchar *remote)
{
    struct stat local_stat;
    LIBSSH2_SFTP_ATTRIBUTES remote_stat;
    SFTPSession *session;
    gint time_cmp;

    if (plugin_data->current_connection < 0 ||
        !plugin_data->sessions[plugin_data->current_connection]) {
        g_printerr("Not connected to server\n");
        return FALSE;
    }

    session = plugin_data->sessions[plugin_data->current_connection];

    /* Get file info */
    if (stat(local, &local_stat) != 0) {
        g_printerr("Cannot get local file info: %s\n", local);
        return FALSE;
    }

    if (libssh2_sftp_stat(session->sftp_session, remote, &remote_stat) != 0) {
        g_printerr("Cannot get remote file info: %s\n", remote);
        return FALSE;
    }

    /* Compare modification time */
    time_cmp = compare_file_time(local_stat.st_mtime, remote_stat.mtime);

    if (time_cmp < 0) {
        g_print("Local file is older, downloading from remote\n");
        return sync_download_file(plugin_data, remote, local);
    } else if (time_cmp > 0) {
        g_print("Local file is newer, uploading to remote\n");
        return sync_upload_file(plugin_data, local, remote);
    } else {
        g_print("File times are the same, no sync needed\n");
        return TRUE;
    }
}

/*
 * Show sync dialog
 */
gboolean sync_show_sync_dialog(SFTPPluginData *plugin_data, const gchar *local)
{
    GtkWidget *dialog;
    GtkWidget *content_area;
    GtkWidget *label;
    GtkWidget *vbox;
    gint result;
    gchar remote_path[MAX_PATH_LEN];

    /* Build remote path */
    gchar *base = g_path_get_basename(local);
    if (plugin_data->current_remote_path[strlen(plugin_data->current_remote_path) - 1] == '/') {
        g_snprintf(remote_path, MAX_PATH_LEN, "%s%s",
                  plugin_data->current_remote_path, base);
    } else {
        g_snprintf(remote_path, MAX_PATH_LEN, "%s/%s",
                  plugin_data->current_remote_path, base);
    }
    g_free(base);

    /* Create dialog */
    dialog = gtk_dialog_new_with_buttons("File Sync", NULL, GTK_DIALOG_MODAL,
                                        "_Cancel", GTK_RESPONSE_CANCEL,
                                        "Compare", 1,
                                        "Upload", 2,
                                        "Download", 3,
                                        "Auto", 4,
                                        "_OK", GTK_RESPONSE_OK, NULL);

    content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));

    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_add(GTK_CONTAINER(content_area), vbox);

    gchar *label_text = g_strdup_printf("Local file: %s\nRemote path: %s", local, remote_path);
    label = gtk_label_new(label_text);
    g_free(label_text);
    gtk_box_pack_start(GTK_BOX(vbox), label, TRUE, TRUE, 10);

    gtk_widget_show_all(vbox);

    result = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);

    switch (result) {
        case 1: /* Compare */
            return sync_compare_files(plugin_data, local, remote_path);
        case 2: /* Upload */
            return sync_upload_file(plugin_data, local, remote_path);
        case 3: /* Download */
            return sync_download_file(plugin_data, remote_path, local);
        case 4: /* Auto */
            return sync_auto_sync(plugin_data, local, remote_path);
        default:
            return FALSE;
    }
}
