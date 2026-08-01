#include "univ.h"

#include <gio/gio.h>
#include <glib.h>

#include <algorithm>
#include <cstring>
#include <utility>

namespace catalog {

std::string univ_bin() {
    if (const char *env = g_getenv("UNIV_BIN"))
        return env;
    return "univ";
}

static char **argv_from(const std::vector<std::string> &parts) {
    char **argv = g_new0(char *, parts.size() + 1);
    for (size_t i = 0; i < parts.size(); i++)
        argv[i] = g_strdup(parts[i].c_str());
    return argv;
}

namespace {

struct QueryJob {
    std::vector<std::string> args;
    QueryCallback cb;
    bool ok = false;
    std::vector<Package> packages;
    std::string message;
};

}

void query_packages(const std::vector<std::string> &args, QueryCallback cb) {
    auto *job = new QueryJob;
    job->args = args;
    job->cb = std::move(cb);

    GThread *thread = g_thread_new("catalog-query", [](gpointer data) -> gpointer {
        auto *job = static_cast<QueryJob *>(data);

        std::vector<std::string> parts;
        parts.reserve(job->args.size() + 1);
        parts.push_back(univ_bin());
        parts.insert(parts.end(), job->args.begin(), job->args.end());
        char **argv = argv_from(parts);

        char *stdout_buf = nullptr;
        char *stderr_buf = nullptr;
        int status = 0;
        GError *error = nullptr;

        if (g_spawn_sync(nullptr, argv, nullptr, G_SPAWN_SEARCH_PATH, nullptr,
                         nullptr, &stdout_buf, &stderr_buf, &status, &error)) {
            GError *check = nullptr;
            if (g_spawn_check_wait_status(status, &check)) {
                job->ok = true;
                job->message = stdout_buf ? stdout_buf : "";
            } else {
                job->message =
                    check && check->message ? check->message : "command failed";
                g_clear_error(&check);
            }
        } else {
            job->message =
                error && error->message ? error->message : "failed to spawn univ";
            g_clear_error(&error);
        }

        if (!job->ok && job->message.empty()) {
            if (stderr_buf && *stderr_buf)
                job->message = stderr_buf;
            else if (stdout_buf && *stdout_buf)
                job->message = stdout_buf;
            else
                job->message = "unknown error";
        }

        g_free(stdout_buf);
        g_free(stderr_buf);
        g_strfreev(argv);

        if (job->ok) {
            std::string parse_error;
            if (!parse_packages(job->message.c_str(), job->packages,
                                parse_error)) {
                job->ok = false;
                job->message = "could not parse univ output: " + parse_error;
            }
        }

        g_main_context_invoke_full(
            nullptr, G_PRIORITY_DEFAULT,
            [](gpointer data) -> gboolean {
                auto *job = static_cast<QueryJob *>(data);
                QueryCallback cb = std::move(job->cb);
                job->cb = {};
                cb(job->ok, std::move(job->packages), job->message);
                return G_SOURCE_REMOVE;
            },
            job, [](gpointer data) {
                delete static_cast<QueryJob *>(data);
            });

        return nullptr;
    }, job);

    g_thread_unref(thread);
}

namespace {

struct StreamCtx {
    struct StreamJob *job;
    bool is_err;
    GInputStream *stream;
};

struct StreamJob {
    std::vector<std::string> args;
    LineCallback on_line;
    ExitCallback on_exit;

    GSubprocess *proc = nullptr;
    bool out_done = false;
    bool err_done = false;
    bool wait_done = false;
    bool finished = false;
    int exit_code = -1;

    std::string pending_out;
    std::string pending_err;
};

void maybe_finish(StreamJob *job) {
    if (job->finished || !job->out_done || !job->err_done || !job->wait_done)
        return;
    job->finished = true;

    int code = job->exit_code;
    ExitCallback cb = std::move(job->on_exit);
    job->on_exit = {};

    if (job->proc)
        g_object_unref(job->proc);

    cb(code);
    delete job;
}

void stream_read_done(GObject *source, GAsyncResult *result, gpointer data) {
    auto *ctx = static_cast<StreamCtx *>(data);
    StreamJob *job = ctx->job;

    GError *error = nullptr;
    GBytes *bytes = g_input_stream_read_bytes_finish(
        G_INPUT_STREAM(source), result, &error);

    if (error || !bytes || g_bytes_get_size(bytes) == 0) {
        g_clear_error(&error);
        if (bytes)
            g_bytes_unref(bytes);

        std::string tail = ctx->is_err ? job->pending_err : job->pending_out;
        if (!tail.empty()) {
            job->on_line(tail);
            if (ctx->is_err)
                job->pending_err.clear();
            else
                job->pending_out.clear();
        }

        if (ctx->is_err)
            job->err_done = true;
        else
            job->out_done = true;

        g_object_unref(ctx->stream);
        delete ctx;
        maybe_finish(job);
        return;
    }

    std::string text(static_cast<const char *>(g_bytes_get_data(bytes, nullptr)),
                     g_bytes_get_size(bytes));
    g_bytes_unref(bytes);

    std::string &pending = ctx->is_err ? job->pending_err : job->pending_out;
    pending += text;

    size_t pos;
    while ((pos = pending.find('\n')) != std::string::npos) {
        std::string line = pending.substr(0, pos);
        pending.erase(0, pos + 1);
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        job->on_line(line);
    }

    g_input_stream_read_bytes_async(ctx->stream, 4096, G_PRIORITY_DEFAULT,
                                    nullptr, stream_read_done, ctx);
}

void stream_wait_done(GObject *source, GAsyncResult *result, gpointer data) {
    auto *job = static_cast<StreamJob *>(data);
    GError *error = nullptr;
    if (g_subprocess_wait_check_finish(job->proc, result, &error)) {
        job->exit_code = g_subprocess_get_exit_status(job->proc);
    } else {
        job->exit_code = -1;
        g_clear_error(&error);
    }
    job->wait_done = true;
    maybe_finish(job);
}

}

void stream_task(const std::vector<std::string> &args, LineCallback on_line,
                 ExitCallback on_exit) {
    auto *job = new StreamJob;
    job->args = args;
    job->on_line = std::move(on_line);
    job->on_exit = std::move(on_exit);

    std::vector<std::string> parts;
    parts.reserve(args.size() + 1);
    parts.push_back(univ_bin());
    parts.insert(parts.end(), args.begin(), args.end());
    char **argv = argv_from(parts);

    GError *error = nullptr;
    GSubprocessLauncher *launcher = g_subprocess_launcher_new(
        static_cast<GSubprocessFlags>(G_SUBPROCESS_FLAGS_STDOUT_PIPE |
                                      G_SUBPROCESS_FLAGS_STDERR_PIPE));
    job->proc = g_subprocess_launcher_spawnv(launcher, argv, &error);
    g_object_unref(launcher);
    g_strfreev(argv);

    if (!job->proc) {
        std::string msg =
            error && error->message ? error->message : "failed to launch univ";
        g_clear_error(&error);
        job->on_line("error: " + msg);
        job->exit_code = -1;
        job->out_done = job->err_done = job->wait_done = true;
        maybe_finish(job);
        return;
    }

    g_subprocess_wait_async(job->proc, nullptr, stream_wait_done, job);

    GInputStream *stdout_pipe = g_subprocess_get_stdout_pipe(job->proc);
    GInputStream *stderr_pipe = g_subprocess_get_stderr_pipe(job->proc);

    auto *out_ctx = new StreamCtx;
    out_ctx->job = job;
    out_ctx->is_err = false;
    out_ctx->stream = g_object_ref(stdout_pipe);

    auto *err_ctx = new StreamCtx;
    err_ctx->job = job;
    err_ctx->is_err = true;
    err_ctx->stream = g_object_ref(stderr_pipe);

    g_input_stream_read_bytes_async(out_ctx->stream, 4096, G_PRIORITY_DEFAULT,
                                    nullptr, stream_read_done, out_ctx);
    g_input_stream_read_bytes_async(err_ctx->stream, 4096, G_PRIORITY_DEFAULT,
                                    nullptr, stream_read_done, err_ctx);
}
}
