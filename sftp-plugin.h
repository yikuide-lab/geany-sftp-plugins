#ifndef SFTP_PLUGIN_H
#define SFTP_PLUGIN_H

#include <geanyplugin.h>
#include <gtk/gtk.h>

/* 包含libssh2头文件 */
#include <libssh2.h>
#include <libssh2_sftp.h>

/* 插件版本 */
#define SFTP_PLUGIN_VERSION "1.0.0"

/* 常量定义 */
#define MAX_HOSTNAME_LEN 256
#define MAX_USERNAME_LEN 64
#define MAX_PASSWORD_LEN 256
#define MAX_PATH_LEN 4096
#define MAX_CONNECTIONS 10
#define DEFAULT_PORT 22
#define CONNECTION_TIMEOUT 30
#define MAX_SSH_HOSTS 50

/* SSH Config Host entry */
typedef struct {
    gchar name[128];           /* Host alias */
    gchar hostname[MAX_HOSTNAME_LEN];
    gint port;
    gchar username[MAX_USERNAME_LEN];
    gchar identity_file[MAX_PATH_LEN];
} SSHConfigHost;

/* 连接状态 */
typedef enum {
    CONN_DISCONNECTED,
    CONN_CONNECTING,
    CONN_CONNECTED,
    CONN_ERROR
} ConnectionState;

/* 连接配置结构体 */
typedef struct {
    gchar name[128];
    gchar hostname[MAX_HOSTNAME_LEN];
    gint port;
    gchar username[MAX_USERNAME_LEN];
    gchar password[MAX_PASSWORD_LEN];
    gchar private_key[MAX_PATH_LEN];
    gchar remote_dir[MAX_PATH_LEN];
    gboolean use_keyring;
    ConnectionState state;
} SFTPConnection;

/* SFTP会话结构体 */
typedef struct {
    SFTPConnection *config;
    LIBSSH2_SESSION *ssh_session;
    LIBSSH2_SFTP *sftp_session;
    int sock;
    gboolean active;
    gchar temp_dir[MAX_PATH_LEN];  /* Temp directory for downloaded files */
    GMutex lock;                    /* Protects libssh2 session from concurrent access */
} SFTPSession;

/* 文件操作结构体 */
typedef struct _FileOperation FileOperation;

/* 异步传输完成回调类型 */
typedef void (*TransferCallback)(FileOperation *op, gboolean success, gpointer user_data);

struct _FileOperation {
    gchar local_path[MAX_PATH_LEN];
    gchar remote_path[MAX_PATH_LEN];
    gboolean is_upload;
    gsize total_size;
    gsize transferred;
    gboolean completed;
    gboolean cancelled;
    gboolean success;
    GThread *thread;
    /* Async callback context */
    SFTPSession *session;
    TransferCallback callback;
    gpointer user_data;
};

/* 插件数据结构体 */
typedef struct {
    GeanyPlugin *geany_plugin;
    GeanyData *geany_data;
    
    /* 菜单项 */
    GtkWidget *menu_item;
    GtkWidget *sidebar;
    
    /* 连接管理 */
    SFTPConnection connections[MAX_CONNECTIONS];
    gint num_connections;
    SFTPSession *sessions[MAX_CONNECTIONS];
    
    /* 当前活动连接 */
    gint current_connection;
    gchar current_remote_path[MAX_PATH_LEN];
    
    /* UI组件 */
    GtkWidget *connection_combo;
    GtkWidget *connect_btn;  /* Connect/Disconnect button */
    GtkWidget *upload_btn;   /* Upload button */
    GtkWidget *refresh_btn;  /* Refresh button */
    GtkWidget *file_treeview;
    GtkWidget *path_entry;  /* Editable path entry */
    GtkWidget *statusbar_label;
    GtkWidget *config_conn_list;  /* Connection list in config dialog */
    
    /* 文件操作 */
    GList *active_operations;
    GList *completed_operations;

    /* Track downloaded files: local_path -> remote_path */
    GHashTable *downloaded_files;
    
    /* 配置 */
    gboolean auto_upload;
    gboolean show_hidden_files;
    gint default_timeout;
} SFTPPluginData;

/* 外部函数声明 */
gboolean sftp_connection_connect(SFTPSession *session);
void sftp_connection_disconnect(SFTPSession *session);
gboolean sftp_list_directory(SFTPSession *session, const gchar *path);
gboolean sftp_upload_file(SFTPSession *session, const gchar *local, const gchar *remote,
                          FileOperation *op);
gboolean sftp_download_file(SFTPSession *session, const gchar *remote, const gchar *local,
                            FileOperation *op);

/* 配置管理函数 */
gboolean config_load_connections(SFTPPluginData *plugin_data);
gboolean config_save_connections(SFTPPluginData *plugin_data);
gboolean config_load_settings(SFTPPluginData *plugin_data);
gboolean config_save_settings(SFTPPluginData *plugin_data);

/* SSH Config 解析函数 */
gint config_load_ssh_hosts(SSHConfigHost *hosts, gint max_hosts);

/* UI函数 */
void ui_create_sidebar(SFTPPluginData *plugin_data);
void ui_update_file_list(SFTPPluginData *plugin_data);
void ui_show_progress_dialog(SFTPPluginData *plugin_data, FileOperation *op);

/* 异步文件传输 */
FileOperation *transfer_async(SFTPSession *session, const gchar *local,
                              const gchar *remote, gboolean is_upload,
                              TransferCallback callback, gpointer user_data);

/* 同步函数 */
gboolean sync_compare_files(SFTPPluginData *plugin_data, const gchar *local, const gchar *remote);
gboolean sync_upload_file(SFTPPluginData *plugin_data, const gchar *local, const gchar *remote);
gboolean sync_download_file(SFTPPluginData *plugin_data, const gchar *remote, const gchar *local);

#endif /* SFTP_PLUGIN_H */