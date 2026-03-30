/*! \file db_sqlite.h
 * \brief SQLite database backend for TinyMUX:Next.
 * \author Diablerie@COR (2026)
 *
 * Enabled at build time with: ./configure --enable-sqlite
 *
 * The SQLite backend is a drop-in replacement for the flatfile I/O layer.
 * The in-memory object model (OBJ *db) is unchanged — SQLite is purely a
 * persistence adapter.  On first run it auto-migrates from the existing
 * flatfile; subsequent starts load directly from SQLite.
 */

#pragma once
#ifndef DB_SQLITE_H
#define DB_SQLITE_H

// Returns true if path exists and contains a valid TinyMUX:Next SQLite DB.
bool db_sqlite_exists(const char *path);

// Load the SQLite database at path into the in-memory db[] array.
// Populates mudstate.db_top, mudstate.attr_next, mudstate.record_players.
// Returns true on success.
bool db_sqlite_read(const char *path);

// Persist the in-memory db[] array to the SQLite database at path.
// Runs inside a single WAL transaction — safe to call during checkpoints
// and panic dumps without the temp-file dance.
// Returns true on success.
bool db_sqlite_write(const char *path);

#endif // DB_SQLITE_H
