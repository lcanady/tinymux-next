/*! \file db_sqlite.cpp
 * \brief SQLite database backend for TinyMUX:Next.
 * \author Diablerie@COR (2026)
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"
#include "db_sqlite.h"
#include "attrs.h"
#include "vattr.h"

#include <sqlite3.h>
#include <cstring>
#include <string>

// ===========================================================================
// Schema
// ===========================================================================

static const char SCHEMA_SQL[] = R"SQL(
PRAGMA journal_mode = WAL;
PRAGMA synchronous  = NORMAL;
PRAGMA foreign_keys = ON;

CREATE TABLE IF NOT EXISTS meta (
    key   TEXT PRIMARY KEY,
    value TEXT NOT NULL DEFAULT ''
);

CREATE TABLE IF NOT EXISTS objects (
    dbref    INTEGER PRIMARY KEY,
    name     TEXT    NOT NULL DEFAULT '',
    location INTEGER NOT NULL DEFAULT -1,
    contents INTEGER NOT NULL DEFAULT -1,
    exits    INTEGER NOT NULL DEFAULT -1,
    next     INTEGER NOT NULL DEFAULT -1,
    link     INTEGER NOT NULL DEFAULT -1,
    parent   INTEGER NOT NULL DEFAULT -1,
    owner    INTEGER NOT NULL DEFAULT -1,
    zone     INTEGER NOT NULL DEFAULT -1,
    pennies  INTEGER NOT NULL DEFAULT 0,
    flags1   INTEGER NOT NULL DEFAULT 0,
    flags2   INTEGER NOT NULL DEFAULT 0,
    flags3   INTEGER NOT NULL DEFAULT 0,
    powers1  INTEGER NOT NULL DEFAULT 0,
    powers2  INTEGER NOT NULL DEFAULT 0
);

CREATE TABLE IF NOT EXISTS attributes (
    dbref   INTEGER NOT NULL REFERENCES objects(dbref) ON DELETE CASCADE,
    attrnum INTEGER NOT NULL,
    value   TEXT    NOT NULL DEFAULT '',
    PRIMARY KEY (dbref, attrnum)
);

CREATE TABLE IF NOT EXISTS vattrs (
    attrnum INTEGER PRIMARY KEY,
    name    TEXT    NOT NULL,
    flags   INTEGER NOT NULL DEFAULT 0
);

CREATE INDEX IF NOT EXISTS idx_attr_dbref ON attributes(dbref);
CREATE INDEX IF NOT EXISTS idx_obj_owner  ON objects(owner);
CREATE INDEX IF NOT EXISTS idx_obj_loc    ON objects(location);
)SQL";

// ===========================================================================
// Internal helpers
// ===========================================================================

// Open the database, create schema if needed, return sqlite3* or nullptr.
static sqlite3 *open_db(const char *path, bool read_only = false)
{
    int flags = read_only
                ? SQLITE_OPEN_READONLY
                : (SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE);

    sqlite3 *db = nullptr;
    if (sqlite3_open_v2(path, &db, flags, nullptr) != SQLITE_OK)
    {
        if (db) sqlite3_close(db);
        return nullptr;
    }

    if (!read_only)
    {
        char *errmsg = nullptr;
        if (sqlite3_exec(db, SCHEMA_SQL, nullptr, nullptr, &errmsg) != SQLITE_OK)
        {
            Log.tinyprintf(T("db_sqlite: schema error: %s" ENDLINE),
                           errmsg ? errmsg : "(unknown)");
            sqlite3_free(errmsg);
            sqlite3_close(db);
            return nullptr;
        }
    }

    // Set a generous busy timeout (5 s) for WAL checkpoints.
    sqlite3_busy_timeout(db, 5000);
    return db;
}

static bool exec(sqlite3 *db, const char *sql)
{
    char *errmsg = nullptr;
    const int rc = sqlite3_exec(db, sql, nullptr, nullptr, &errmsg);
    if (rc != SQLITE_OK)
    {
        Log.tinyprintf(T("db_sqlite: %s" ENDLINE), errmsg ? errmsg : sql);
        sqlite3_free(errmsg);
        return false;
    }
    return true;
}

// ===========================================================================
// db_sqlite_exists
// ===========================================================================

bool db_sqlite_exists(const char *path)
{
    sqlite3 *db = nullptr;
    if (sqlite3_open_v2(path, &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK)
    {
        if (db) sqlite3_close(db);
        return false;
    }

    // Verify the objects table exists (proves this is our schema, not a
    // random SQLite file that happens to be at the path).
    sqlite3_stmt *stmt = nullptr;
    const char *check =
        "SELECT 1 FROM sqlite_master WHERE type='table' AND name='objects' LIMIT 1;";
    bool found = false;
    if (sqlite3_prepare_v2(db, check, -1, &stmt, nullptr) == SQLITE_OK)
    {
        found = (sqlite3_step(stmt) == SQLITE_ROW);
        sqlite3_finalize(stmt);
    }
    sqlite3_close(db);
    return found;
}

// ===========================================================================
// db_sqlite_read
// ===========================================================================

bool db_sqlite_read(const char *path)
{
    sqlite3 *sdb = open_db(path);
    if (!sdb) return false;

    STARTLOG(LOG_STARTUP, "INI", "SQLT")
    log_text(T("Loading from SQLite: "));
    log_text(reinterpret_cast<const UTF8 *>(path));
    ENDLOG

    // -----------------------------------------------------------------------
    // 1. Meta
    // -----------------------------------------------------------------------
    {
        sqlite3_stmt *stmt = nullptr;
        const char *sql = "SELECT key, value FROM meta;";
        if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) == SQLITE_OK)
        {
            while (sqlite3_step(stmt) == SQLITE_ROW)
            {
                const char *key = reinterpret_cast<const char *>(
                                      sqlite3_column_text(stmt, 0));
                const int   val = sqlite3_column_int(stmt, 1);
                if      (strcmp(key, "db_top")         == 0) mudstate.min_size = val;
                else if (strcmp(key, "attr_next")       == 0) mudstate.attr_next = val;
                else if (strcmp(key, "record_players")  == 0) mudstate.record_players = val;
            }
            sqlite3_finalize(stmt);
        }
    }

    // -----------------------------------------------------------------------
    // 2. vattrs (user-named attributes)
    // -----------------------------------------------------------------------
    {
        sqlite3_stmt *stmt = nullptr;
        const char *sql = "SELECT attrnum, name, flags FROM vattrs ORDER BY attrnum;";
        if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) == SQLITE_OK)
        {
            while (sqlite3_step(stmt) == SQLITE_ROW)
            {
                const int     num   = sqlite3_column_int(stmt, 0);
                const UTF8   *name  = reinterpret_cast<const UTF8 *>(
                                          sqlite3_column_text(stmt, 1));
                const int     flags = sqlite3_column_int(stmt, 2);
                const size_t  nName = strlen(reinterpret_cast<const char *>(name));
                vattr_define_LEN(name, nName, num, flags);
                if (mudstate.attr_next <= num)
                    mudstate.attr_next = num + 1;
            }
            sqlite3_finalize(stmt);
        }
    }

    // -----------------------------------------------------------------------
    // 3. Objects
    // -----------------------------------------------------------------------
    {
        sqlite3_stmt *stmt = nullptr;
        const char *sql =
            "SELECT dbref, name, location, contents, exits, next, link,"
            "       parent, owner, zone, pennies,"
            "       flags1, flags2, flags3, powers1, powers2"
            "  FROM objects ORDER BY dbref;";

        if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) != SQLITE_OK)
        {
            Log.tinyprintf(T("db_sqlite: prepare objects: %s" ENDLINE),
                           sqlite3_errmsg(sdb));
            sqlite3_close(sdb);
            return false;
        }

        dbref db_top = 0;
        while (sqlite3_step(stmt) == SQLITE_ROW)
        {
            const dbref i = sqlite3_column_int(stmt, 0);
            db_grow(i + 1);
            if (i >= db_top) db_top = i + 1;

            const UTF8 *name = reinterpret_cast<const UTF8 *>(
                                   sqlite3_column_text(stmt, 1));
            if (name && *name)
            {
                // s_Name goes through the attribute system
                UTF8 *buf = alloc_mbuf("db_sqlite_read.name");
                StripTabsAndTruncate(name, buf, MBUF_SIZE - 1, MBUF_SIZE - 1);
                s_Name(i, buf);
                free_mbuf(buf);
            }

            s_Location(i, sqlite3_column_int(stmt, 2));
            s_Contents(i, sqlite3_column_int(stmt, 3));
            s_Exits(i,    sqlite3_column_int(stmt, 4));
            s_Next(i,     sqlite3_column_int(stmt, 5));
            s_Link(i,     sqlite3_column_int(stmt, 6));
            s_Parent(i,   sqlite3_column_int(stmt, 7));
            s_Owner(i,    sqlite3_column_int(stmt, 8));
            s_Zone(i,     sqlite3_column_int(stmt, 9));
            s_PenniesDirect(i, sqlite3_column_int(stmt, 10));

            db[i].fs.word[FLAG_WORD1] = static_cast<FLAG>(sqlite3_column_int(stmt, 11));
            db[i].fs.word[FLAG_WORD2] = static_cast<FLAG>(sqlite3_column_int(stmt, 12));
            db[i].fs.word[FLAG_WORD3] = static_cast<FLAG>(sqlite3_column_int(stmt, 13));
            s_Powers(i,  sqlite3_column_int(stmt, 14));
            s_Powers2(i, sqlite3_column_int(stmt, 15));
        }
        sqlite3_finalize(stmt);

        if (db_top > mudstate.db_top)
            mudstate.db_top = db_top;
    }

    // -----------------------------------------------------------------------
    // 4. Attributes
    // -----------------------------------------------------------------------
    {
        sqlite3_stmt *stmt = nullptr;
        const char *sql = "SELECT dbref, attrnum, value FROM attributes;";
        if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) == SQLITE_OK)
        {
            while (sqlite3_step(stmt) == SQLITE_ROW)
            {
                const dbref  i   = sqlite3_column_int(stmt, 0);
                const int    atr = sqlite3_column_int(stmt, 1);
                const UTF8  *val = reinterpret_cast<const UTF8 *>(
                                       sqlite3_column_text(stmt, 2));
                const size_t len = static_cast<size_t>(sqlite3_column_bytes(stmt, 2));
                if (Good_dbref(i) && i < mudstate.db_top)
                    atr_add_raw_LEN(i, atr, val, len);
            }
            sqlite3_finalize(stmt);
        }
    }

    sqlite3_close(sdb);

    STARTLOG(LOG_STARTUP, "INI", "SQLT")
    log_text(T("SQLite load complete. db_top="));
    log_number(mudstate.db_top);
    ENDLOG

    return true;
}

// ===========================================================================
// db_sqlite_write
// ===========================================================================

bool db_sqlite_write(const char *path)
{
    sqlite3 *sdb = open_db(path);
    if (!sdb) return false;

    // Single transaction — makes the whole checkpoint atomic and fast.
    if (!exec(sdb, "BEGIN IMMEDIATE;"))
    {
        sqlite3_close(sdb);
        return false;
    }

    // -----------------------------------------------------------------------
    // 1. Meta
    // -----------------------------------------------------------------------
    {
        const char *sql =
            "INSERT OR REPLACE INTO meta(key, value) VALUES(?,?);";
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) == SQLITE_OK)
        {
            auto put_meta = [&](const char *key, int val)
            {
                sqlite3_bind_text(stmt, 1, key, -1, SQLITE_STATIC);
                sqlite3_bind_int(stmt,  2, val);
                sqlite3_step(stmt);
                sqlite3_reset(stmt);
            };
            put_meta("db_top",        mudstate.db_top);
            put_meta("attr_next",     mudstate.attr_next);
            put_meta("record_players",mudstate.record_players);
            sqlite3_finalize(stmt);
        }
    }

    // -----------------------------------------------------------------------
    // 2. vattrs — replace all
    // -----------------------------------------------------------------------
    exec(sdb, "DELETE FROM vattrs;");
    {
        const char *sql =
            "INSERT INTO vattrs(attrnum, name, flags) VALUES(?,?,?);";
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) == SQLITE_OK)
        {
            for (ATTR *va = vattr_first(); va; va = vattr_next(va))
            {
                if (va->flags & AF_DELETED) continue;
                sqlite3_bind_int(stmt,  1, va->number);
                sqlite3_bind_text(stmt, 2,
                    reinterpret_cast<const char *>(va->name), -1, SQLITE_STATIC);
                sqlite3_bind_int(stmt,  3, va->flags);
                sqlite3_step(stmt);
                sqlite3_reset(stmt);
            }
            sqlite3_finalize(stmt);
        }
    }

    // -----------------------------------------------------------------------
    // 3. Objects — replace all non-garbage
    // -----------------------------------------------------------------------
    exec(sdb, "DELETE FROM objects;");  // cascade deletes attributes too
    {
        const char *sql =
            "INSERT INTO objects"
            "  (dbref, name, location, contents, exits, next, link,"
            "   parent, owner, zone, pennies,"
            "   flags1, flags2, flags3, powers1, powers2)"
            "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?);";
        sqlite3_stmt *ostmt = nullptr;

        const char *asql =
            "INSERT INTO attributes(dbref, attrnum, value) VALUES(?,?,?);";
        sqlite3_stmt *astmt = nullptr;

        if (sqlite3_prepare_v2(sdb, sql,  -1, &ostmt, nullptr) != SQLITE_OK ||
            sqlite3_prepare_v2(sdb, asql, -1, &astmt, nullptr) != SQLITE_OK)
        {
            Log.tinyprintf(T("db_sqlite: prepare write: %s" ENDLINE),
                           sqlite3_errmsg(sdb));
            if (ostmt) sqlite3_finalize(ostmt);
            if (astmt) sqlite3_finalize(astmt);
            exec(sdb, "ROLLBACK;");
            sqlite3_close(sdb);
            return false;
        }

        for (dbref i = 0; i < mudstate.db_top; ++i)
        {
            if (isGarbage(i)) continue;

            const UTF8 *name = Name(i);

            sqlite3_bind_int(ostmt, 1,  i);
            sqlite3_bind_text(ostmt, 2,
                name ? reinterpret_cast<const char *>(name) : "",
                -1, SQLITE_STATIC);
            sqlite3_bind_int(ostmt, 3,  Location(i));
            sqlite3_bind_int(ostmt, 4,  Contents(i));
            sqlite3_bind_int(ostmt, 5,  Exits(i));
            sqlite3_bind_int(ostmt, 6,  Next(i));
            sqlite3_bind_int(ostmt, 7,  Link(i));
            sqlite3_bind_int(ostmt, 8,  Parent(i));
            sqlite3_bind_int(ostmt, 9,  Owner(i));
            sqlite3_bind_int(ostmt, 10, Zone(i));
            sqlite3_bind_int(ostmt, 11, Pennies(i));
            sqlite3_bind_int(ostmt, 12, static_cast<int>(db[i].fs.word[FLAG_WORD1]));
            sqlite3_bind_int(ostmt, 13, static_cast<int>(db[i].fs.word[FLAG_WORD2]));
            sqlite3_bind_int(ostmt, 14, static_cast<int>(db[i].fs.word[FLAG_WORD3]));
            sqlite3_bind_int(ostmt, 15, db[i].powers);
            sqlite3_bind_int(ostmt, 16, db[i].powers2);
            sqlite3_step(ostmt);
            sqlite3_reset(ostmt);

            // Attributes for this object
            unsigned char *as = nullptr;
            for (int ca = atr_head(i, &as); ca; ca = atr_next(&as))
            {
                const ATTR *a = atr_num(ca);
                if (!a) continue;
                const int j = a->number;

                // Skip internal attrs that are written inline as object fields
                if (j == A_LIST || j == A_MONEY) continue;

                const UTF8 *val = atr_get_raw(i, j);
                if (!val) continue;

                sqlite3_bind_int(astmt,  1, i);
                sqlite3_bind_int(astmt,  2, j);
                sqlite3_bind_text(astmt, 3,
                    reinterpret_cast<const char *>(val), -1, SQLITE_STATIC);
                sqlite3_step(astmt);
                sqlite3_reset(astmt);
            }
        }

        sqlite3_finalize(ostmt);
        sqlite3_finalize(astmt);
    }

    const bool ok = exec(sdb, "COMMIT;");
    if (!ok) exec(sdb, "ROLLBACK;");
    sqlite3_close(sdb);

    return ok;
}
