/*
 * Connection Management Module
 * Implement SFTP connection using libssh2
 */

#include "sftp-plugin.h"
#include "compat.h"

#include <fcntl.h>
#include <errno.h>
#include <string.h>

/*
 * Connect to SFTP server
 */
gboolean sftp_connection_connect(SFTPSession *session)
{
    SFTPConnection *config = session->config;
    LIBSSH2_SESSION *ssh;
    LIBSSH2_SFTP *sftp;
    int sock;
    int rc;

    if (!config) {
        g_printerr("Connection config is empty\n");
        return FALSE;
    }

    config->state = CONN_CONNECTING;

    /* Create socket connection */
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        g_printerr("Failed to create socket\n");
        config->state = CONN_ERROR;
        return FALSE;
    }

    /* Connect to server */
    struct hostent *host = gethostbyname(config->hostname);
    if (!host) {
        g_printerr("Cannot resolve hostname: %s\n", config->hostname);
        compat_close_socket(sock);
        config->state = CONN_ERROR;
        return FALSE;
    }

    struct sockaddr_in sin;
    sin.sin_family = host->h_addrtype;
    sin.sin_port = htons(config->port);
    sin.sin_addr = *((struct in_addr *)host->h_addr);

    if (connect(sock, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
        g_printerr("Failed to connect to server: %s\n", strerror(errno));
        compat_close_socket(sock);
        config->state = CONN_ERROR;
        return FALSE;
    }

    session->sock = sock;

    /* Create SSH session */
    ssh = libssh2_session_init();
    if (!ssh) {
        g_printerr("Failed to initialize SSH session\n");
        compat_close_socket(sock);
        config->state = CONN_ERROR;
        return FALSE;
    }

    /* Set to non-blocking mode */
    libssh2_session_set_blocking(ssh, 0);

    /* Perform SSH handshake */
    while ((rc = libssh2_session_handshake(ssh, sock)) == LIBSSH2_ERROR_EAGAIN) {
        /* Wait for socket to be writable */
        struct timeval tv;
        fd_set fd;
        FD_ZERO(&fd);
        FD_SET(sock, &fd);
        tv.tv_sec = 0;
        tv.tv_usec = 100000;
        select(sock + 1, NULL, &fd, NULL, &tv);
    }

    if (rc) {
        g_printerr("SSH handshake failed: %d\n", rc);
        libssh2_session_free(ssh);
        compat_close_socket(sock);
        config->state = CONN_ERROR;
        return FALSE;
    }

    /* Set to blocking mode */
    libssh2_session_set_blocking(ssh, 1);

    /* Authentication */
    const char *auth_methods = libssh2_userauth_list(ssh, config->username, strlen(config->username));

    if (auth_methods && strstr(auth_methods, "publickey") &&
        config->private_key[0] != '\0') {
        /* Use public key authentication */
        rc = libssh2_userauth_publickey_fromfile(ssh, config->username,
                                                  NULL, config->private_key, NULL);
    } else {
        /* Use password authentication */
        rc = libssh2_userauth_password(ssh, config->username, config->password);
    }

    if (rc) {
        g_printerr("Authentication failed: %d\n", rc);
        libssh2_session_free(ssh);
        compat_close_socket(sock);
        config->state = CONN_ERROR;
        return FALSE;
    }

    /* Create SFTP session */
    sftp = libssh2_sftp_init(ssh);
    if (!sftp) {
        g_printerr("Failed to initialize SFTP session\n");
        libssh2_session_free(ssh);
        compat_close_socket(sock);
        config->state = CONN_ERROR;
        return FALSE;
    }

    session->ssh_session = ssh;
    session->sftp_session = sftp;
    session->active = TRUE;
    config->state = CONN_CONNECTED;

    g_print("Successfully connected to server: %s\n", config->hostname);
    return TRUE;
}

/*
 * Disconnect SFTP connection
 */
void sftp_connection_disconnect(SFTPSession *session)
{
    if (!session)
        return;

    if (session->sftp_session) {
        libssh2_sftp_shutdown(session->sftp_session);
        session->sftp_session = NULL;
    }

    if (session->ssh_session) {
        libssh2_session_disconnect(session->ssh_session, "Normal disconnect");
        libssh2_session_free(session->ssh_session);
        session->ssh_session = NULL;
    }

    if (session->sock > 0) {
        compat_close_socket(session->sock);
        session->sock = 0;
    }

    if (session->config) {
        session->config->state = CONN_DISCONNECTED;
    }

    session->active = FALSE;
    g_print("Connection disconnected\n");
}

/*
 * List remote directory contents
 */
gboolean sftp_list_directory(SFTPSession *session, const gchar *path)
{
    LIBSSH2_SFTP_HANDLE *handle;
    LIBSSH2_SFTP_ATTRIBUTES attrs;
    char filename[MAX_PATH_LEN];
    int rc;

    if (!session || !session->active || !session->sftp_session) {
        g_printerr("Not connected to server\n");
        return FALSE;
    }

    handle = libssh2_sftp_opendir(session->sftp_session, path);
    if (!handle) {
        g_printerr("Cannot open directory: %s\n", path);
        return FALSE;
    }

    g_print("Directory contents: %s\n", path);
    g_print("----------------------------------------\n");

    do {
        rc = libssh2_sftp_readdir(handle, filename, sizeof(filename), &attrs);
        if (rc > 0) {
            const char *type = (attrs.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS) &&
                             (attrs.permissions & LIBSSH2_SFTP_S_IFDIR) ? "DIR" : "FILE";
            g_print("%-20s %s\n", type, filename);
        }
    } while (rc > 0);

    libssh2_sftp_closedir(handle);

    return TRUE;
}

/*
 * Upload file
 */
gboolean sftp_upload_file(SFTPSession *session, const gchar *local, const gchar *remote,
                          FileOperation *op)
{
    FILE *local_file;
    LIBSSH2_SFTP_HANDLE *sftp_handle;
    char buf[8192];
    size_t nread;
    int rc;

    if (!session || !session->active || !session->sftp_session) {
        g_printerr("Not connected to server\n");
        return FALSE;
    }

    local_file = fopen(local, "rb");
    if (!local_file) {
        g_printerr("Cannot open local file: %s\n", local);
        return FALSE;
    }

    /* Get file size for progress */
    if (op) {
        fseek(local_file, 0, SEEK_END);
        op->total_size = (gsize)ftell(local_file);
        fseek(local_file, 0, SEEK_SET);
        op->transferred = 0;
    }

    sftp_handle = libssh2_sftp_open(session->sftp_session, remote,
                                    LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT | LIBSSH2_FXF_TRUNC,
                                    LIBSSH2_SFTP_S_IRUSR | LIBSSH2_SFTP_S_IWUSR);
    if (!sftp_handle) {
        g_printerr("Cannot create remote file: %s\n", remote);
        fclose(local_file);
        return FALSE;
    }

    g_print("Uploading: %s -> %s\n", local, remote);

    while ((nread = fread(buf, 1, sizeof(buf), local_file)) > 0) {
        size_t written = 0;
        while (written < nread) {
            if (op && op->cancelled) {
                libssh2_sftp_close(sftp_handle);
                fclose(local_file);
                return FALSE;
            }
            rc = libssh2_sftp_write(sftp_handle, buf + written, nread - written);
            if (rc < 0) {
                g_printerr("Upload failed: %d\n", rc);
                libssh2_sftp_close(sftp_handle);
                fclose(local_file);
                return FALSE;
            }
            written += rc;
            if (op)
                g_atomic_pointer_add(&op->transferred, rc);
        }
    }

    libssh2_sftp_close(sftp_handle);
    fclose(local_file);

    g_print("Upload completed\n");
    return TRUE;
}

/*
 * Download file
 */
gboolean sftp_download_file(SFTPSession *session, const gchar *remote, const gchar *local,
                            FileOperation *op)
{
    FILE *local_file;
    LIBSSH2_SFTP_HANDLE *sftp_handle;
    char buf[8192];
    int rc;
    LIBSSH2_SFTP_ATTRIBUTES attrs;

    if (!session || !session->active || !session->sftp_session) {
        g_printerr("Not connected to server\n");
        return FALSE;
    }

    sftp_handle = libssh2_sftp_open(session->sftp_session, remote, LIBSSH2_FXF_READ, 0);
    if (!sftp_handle) {
        g_printerr("Cannot open remote file: %s\n", remote);
        return FALSE;
    }

    /* Get file size for progress */
    if (libssh2_sftp_stat(session->sftp_session, remote, &attrs) == 0) {
        g_print("File size: %lu bytes\n", (unsigned long)attrs.filesize);
        if (op)
            op->total_size = (gsize)attrs.filesize;
    }
    if (op)
        op->transferred = 0;

    local_file = fopen(local, "wb");
    if (!local_file) {
        g_printerr("Cannot create local file: %s\n", local);
        libssh2_sftp_close(sftp_handle);
        return FALSE;
    }

    g_print("Downloading: %s -> %s\n", remote, local);

    while ((rc = libssh2_sftp_read(sftp_handle, buf, sizeof(buf))) > 0) {
        if (op && op->cancelled) {
            libssh2_sftp_close(sftp_handle);
            fclose(local_file);
            return FALSE;
        }
        if (fwrite(buf, 1, rc, local_file) != (size_t)rc) {
            g_printerr("Failed to write local file\n");
            libssh2_sftp_close(sftp_handle);
            fclose(local_file);
            return FALSE;
        }
        if (op)
            g_atomic_pointer_add(&op->transferred, rc);
    }

    libssh2_sftp_close(sftp_handle);
    fclose(local_file);

    g_print("Download completed\n");
    return TRUE;
}

/*
 * Idle callback - runs on main thread after transfer completes
 */
static gboolean transfer_complete_idle(gpointer data)
{
    FileOperation *op = (FileOperation *)data;
    if (op->callback)
        op->callback(op, op->success, op->user_data);
    return G_SOURCE_REMOVE;
}

/*
 * Worker thread function
 */
static gpointer transfer_thread_func(gpointer data)
{
    FileOperation *op = (FileOperation *)data;

    g_mutex_lock(&op->session->lock);

    if (op->is_upload)
        op->success = sftp_upload_file(op->session, op->local_path, op->remote_path, op);
    else
        op->success = sftp_download_file(op->session, op->remote_path, op->local_path, op);

    g_mutex_unlock(&op->session->lock);

    op->completed = TRUE;
    g_idle_add(transfer_complete_idle, op);
    return NULL;
}

/*
 * Start an async file transfer. Caller must free the returned FileOperation
 * in the callback (or after completion).
 */
FileOperation *transfer_async(SFTPSession *session, const gchar *local,
                              const gchar *remote, gboolean is_upload,
                              TransferCallback callback, gpointer user_data)
{
    FileOperation *op = g_new0(FileOperation, 1);
    g_strlcpy(op->local_path, local, MAX_PATH_LEN);
    g_strlcpy(op->remote_path, remote, MAX_PATH_LEN);
    op->is_upload = is_upload;
    op->session = session;
    op->callback = callback;
    op->user_data = user_data;

    op->thread = g_thread_new("sftp-transfer", transfer_thread_func, op);
    return op;
}
