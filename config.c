/*
 * Configuration Management Module
 * Store configuration in JSON format using json-glib
 */

#include "sftp-plugin.h"

#include <sys/stat.h>
#include <glib/gstdio.h>
#include <json-glib/json-glib.h>

#define CONFIG_DIR ".config/geany/plugins/sftp"
#define CONNECTIONS_FILE "connections.json"
#define SETTINGS_FILE "settings.json"

static gchar *get_config_dir(void)
{
    return g_build_filename(g_get_home_dir(), CONFIG_DIR, NULL);
}

static void ensure_config_dir(void)
{
    gchar *config_dir = get_config_dir();
    if (!g_file_test(config_dir, G_FILE_TEST_IS_DIR))
        g_mkdir_with_parents(config_dir, 0755);
    g_free(config_dir);
}

static gchar *get_connections_file(void)
{
    gchar *config_dir = get_config_dir();
    gchar *file = g_build_filename(config_dir, CONNECTIONS_FILE, NULL);
    g_free(config_dir);
    return file;
}

static gchar *get_settings_file(void)
{
    gchar *config_dir = get_config_dir();
    gchar *file = g_build_filename(config_dir, SETTINGS_FILE, NULL);
    g_free(config_dir);
    return file;
}

/* Helper: safely copy a JSON string member into a fixed-size buffer */
static void json_get_string_member_safe(JsonObject *obj, const gchar *name,
                                        gchar *buf, gsize buf_size)
{
    if (json_object_has_member(obj, name) &&
        json_object_get_string_member(obj, name)) {
        g_strlcpy(buf, json_object_get_string_member(obj, name), buf_size);
    }
}

/* Parse a single JSON object into SFTPConnection */
static gboolean parse_connection_object(JsonObject *obj, SFTPConnection *conn)
{
    memset(conn, 0, sizeof(SFTPConnection));
    conn->port = DEFAULT_PORT;
    conn->state = CONN_DISCONNECTED;
    conn->use_keyring = FALSE;
    g_strlcpy(conn->remote_dir, ".", sizeof(conn->remote_dir));

    json_get_string_member_safe(obj, "name", conn->name, sizeof(conn->name));
    json_get_string_member_safe(obj, "hostname", conn->hostname, sizeof(conn->hostname));
    json_get_string_member_safe(obj, "username", conn->username, sizeof(conn->username));
    json_get_string_member_safe(obj, "password", conn->password, sizeof(conn->password));
    json_get_string_member_safe(obj, "private_key", conn->private_key, sizeof(conn->private_key));
    json_get_string_member_safe(obj, "remote_dir", conn->remote_dir, sizeof(conn->remote_dir));

    if (json_object_has_member(obj, "port"))
        conn->port = (gint)json_object_get_int_member(obj, "port");

    return (conn->name[0] && conn->hostname[0]);
}

/* Build a JSON object from SFTPConnection */
static JsonNode *connection_to_node(const SFTPConnection *conn)
{
    JsonObject *obj = json_object_new();
    json_object_set_string_member(obj, "name", conn->name);
    json_object_set_string_member(obj, "hostname", conn->hostname);
    json_object_set_int_member(obj, "port", conn->port);
    json_object_set_string_member(obj, "username", conn->username);
    json_object_set_string_member(obj, "password", conn->password);
    json_object_set_string_member(obj, "private_key", conn->private_key);
    json_object_set_string_member(obj, "remote_dir", conn->remote_dir);

    JsonNode *node = json_node_new(JSON_NODE_OBJECT);
    json_node_take_object(node, obj);
    return node;
}

/* Serialize a JsonNode to a pretty-printed string */
static gchar *node_to_string(JsonNode *root)
{
    JsonGenerator *gen = json_generator_new();
    json_generator_set_pretty(gen, TRUE);
    json_generator_set_indent(gen, 2);
    json_generator_set_root(gen, root);
    gchar *data = json_generator_to_data(gen, NULL);
    g_object_unref(gen);
    return data;
}

gboolean config_load_connections(SFTPPluginData *plugin_data)
{
    gchar *file;
    GError *error = NULL;

    ensure_config_dir();
    file = get_connections_file();

    if (!g_file_test(file, G_FILE_TEST_EXISTS)) {
        g_free(file);
        return TRUE;
    }

    JsonParser *parser = json_parser_new();
    if (!json_parser_load_from_file(parser, file, &error)) {
        g_printerr("Failed to parse connections file: %s\n", error->message);
        g_error_free(error);
        g_object_unref(parser);
        g_free(file);
        return FALSE;
    }

    JsonNode *root = json_parser_get_root(parser);
    if (!root || !JSON_NODE_HOLDS_OBJECT(root)) {
        g_object_unref(parser);
        g_free(file);
        return TRUE;
    }

    JsonObject *root_obj = json_node_get_object(root);
    if (!json_object_has_member(root_obj, "connections")) {
        g_object_unref(parser);
        g_free(file);
        return TRUE;
    }

    JsonArray *arr = json_object_get_array_member(root_obj, "connections");
    guint len = json_array_get_length(arr);
    gint count = 0;

    for (guint i = 0; i < len && count < MAX_CONNECTIONS; i++) {
        JsonObject *obj = json_array_get_object_element(arr, i);
        SFTPConnection conn;
        if (parse_connection_object(obj, &conn)) {
            plugin_data->connections[count++] = conn;
            g_print("Loaded connection: %s (%s:%d)\n", conn.name, conn.hostname, conn.port);
        }
    }

    plugin_data->num_connections = count;
    g_object_unref(parser);
    g_free(file);
    return TRUE;
}

gboolean config_save_connections(SFTPPluginData *plugin_data)
{
    gchar *file;
    GError *error = NULL;

    ensure_config_dir();
    file = get_connections_file();

    g_print("Saving %d connections to %s\n", plugin_data->num_connections, file);

    JsonArray *arr = json_array_new();
    for (gint i = 0; i < plugin_data->num_connections; i++)
        json_array_add_element(arr, connection_to_node(&plugin_data->connections[i]));

    JsonObject *root_obj = json_object_new();
    json_object_set_array_member(root_obj, "connections", arr);

    JsonNode *root = json_node_new(JSON_NODE_OBJECT);
    json_node_take_object(root, root_obj);

    gchar *data = node_to_string(root);
    json_node_free(root);

    gboolean ok = g_file_set_contents(file, data, -1, &error);
    if (!ok) {
        g_printerr("Failed to save config file: %s\n", error->message);
        g_error_free(error);
    } else {
        g_print("Saved %d connection(s)\n", plugin_data->num_connections);
    }

    g_free(data);
    g_free(file);
    return ok;
}

gboolean config_load_settings(SFTPPluginData *plugin_data)
{
    gchar *file;
    GError *error = NULL;

    ensure_config_dir();
    file = get_settings_file();

    /* Defaults */
    plugin_data->auto_upload = FALSE;
    plugin_data->show_hidden_files = FALSE;
    plugin_data->default_timeout = CONNECTION_TIMEOUT;

    if (!g_file_test(file, G_FILE_TEST_EXISTS)) {
        g_free(file);
        return TRUE;
    }

    JsonParser *parser = json_parser_new();
    if (!json_parser_load_from_file(parser, file, &error)) {
        g_printerr("Failed to read settings file: %s\n", error->message);
        g_error_free(error);
        g_object_unref(parser);
        g_free(file);
        return FALSE;
    }

    JsonNode *root = json_parser_get_root(parser);
    if (root && JSON_NODE_HOLDS_OBJECT(root)) {
        JsonObject *obj = json_node_get_object(root);
        if (json_object_has_member(obj, "auto_upload"))
            plugin_data->auto_upload = json_object_get_boolean_member(obj, "auto_upload");
        if (json_object_has_member(obj, "show_hidden_files"))
            plugin_data->show_hidden_files = json_object_get_boolean_member(obj, "show_hidden_files");
        if (json_object_has_member(obj, "default_timeout"))
            plugin_data->default_timeout = (gint)json_object_get_int_member(obj, "default_timeout");
    }

    g_object_unref(parser);
    g_free(file);
    return TRUE;
}

gboolean config_save_settings(SFTPPluginData *plugin_data)
{
    gchar *file;
    GError *error = NULL;

    ensure_config_dir();
    file = get_settings_file();

    JsonObject *obj = json_object_new();
    json_object_set_boolean_member(obj, "auto_upload", plugin_data->auto_upload);
    json_object_set_boolean_member(obj, "show_hidden_files", plugin_data->show_hidden_files);
    json_object_set_int_member(obj, "default_timeout", plugin_data->default_timeout);

    JsonNode *root = json_node_new(JSON_NODE_OBJECT);
    json_node_take_object(root, obj);

    gchar *data = node_to_string(root);
    json_node_free(root);

    gboolean ok = g_file_set_contents(file, data, -1, &error);
    if (!ok) {
        g_printerr("Failed to save settings file: %s\n", error->message);
        g_error_free(error);
    } else {
        g_print("Plugin settings saved\n");
    }

    g_free(data);
    g_free(file);
    return ok;
}

/*
 * Parse ~/.ssh/config and load SSH host entries
 * (No JSON involved, kept as-is)
 */
gint config_load_ssh_hosts(SSHConfigHost *hosts, gint max_hosts)
{
    gchar *ssh_config_path;
    gchar *contents;
    gsize length;
    GError *error = NULL;
    gint count = 0;
    SSHConfigHost *current = NULL;

    ssh_config_path = g_build_filename(g_get_home_dir(), ".ssh", "config", NULL);

    if (!g_file_test(ssh_config_path, G_FILE_TEST_EXISTS)) {
        g_free(ssh_config_path);
        return 0;
    }

    if (!g_file_get_contents(ssh_config_path, &contents, &length, &error)) {
        g_printerr("Failed to read SSH config: %s\n", error->message);
        g_error_free(error);
        g_free(ssh_config_path);
        return 0;
    }

    gchar **lines = g_strsplit(contents, "\n", -1);

    for (gint i = 0; lines[i] != NULL && count < max_hosts; i++) {
        gchar *line = g_strstrip(g_strdup(lines[i]));

        if (line[0] == '#' || line[0] == '\0') {
            g_free(line);
            continue;
        }

        if (g_ascii_strncasecmp(line, "Host ", 5) == 0) {
            gchar *host_value = g_strstrip(g_strdup(line + 5));
            if (strchr(host_value, '*') == NULL && strchr(host_value, '?') == NULL) {
                current = &hosts[count++];
                memset(current, 0, sizeof(SSHConfigHost));
                g_strlcpy(current->name, host_value, sizeof(current->name));
                current->port = 22;
            } else {
                current = NULL;
            }
            g_free(host_value);
        } else if (current && g_ascii_strncasecmp(line, "HostName ", 9) == 0) {
            g_strlcpy(current->hostname, g_strstrip(line + 9), sizeof(current->hostname));
        } else if (current && g_ascii_strncasecmp(line, "Hostname ", 9) == 0) {
            g_strlcpy(current->hostname, g_strstrip(line + 9), sizeof(current->hostname));
        } else if (current && g_ascii_strncasecmp(line, "Port ", 5) == 0) {
            current->port = atoi(g_strstrip(line + 5));
        } else if (current && g_ascii_strncasecmp(line, "User ", 5) == 0) {
            g_strlcpy(current->username, g_strstrip(line + 5), sizeof(current->username));
        } else if (current && g_ascii_strncasecmp(line, "IdentityFile ", 13) == 0) {
            gchar *path = g_strstrip(line + 13);
            if (path[0] == '~') {
                gchar *expanded = g_build_filename(g_get_home_dir(), path + 1, NULL);
                g_strlcpy(current->identity_file, expanded, sizeof(current->identity_file));
                g_free(expanded);
            } else {
                g_strlcpy(current->identity_file, path, sizeof(current->identity_file));
            }
        }

        g_free(line);
    }

    g_strfreev(lines);
    g_free(contents);
    g_free(ssh_config_path);

    g_print("Loaded %d SSH hosts from config\n", count);
    return count;
}
