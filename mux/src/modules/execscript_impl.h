/*! \file execscript_impl.h
 * \brief ExecScript — pure-logic helpers shared between the module and unit tests.
 *
 * This header contains functions that have no TinyMUX framework dependency so
 * they can be included directly by the Catch2 test binary.
 */
#ifndef EXECSCRIPT_IMPL_H
#define EXECSCRIPT_IMPL_H

// Returns true if `name` is a safe script name:
//   - non-empty
//   - does not start with '.' (rejects "." and "..")
//   - contains only [A-Za-z0-9._-]
//   - no directory separator '/'
inline bool safe_script_name(const char *name)
{
    if (name == nullptr || *name == '\0') return false;

    // Reject names starting with '.' (covers ".", "..", ".hidden")
    if (name[0] == '.') return false;

    for (const char *p = name; *p; ++p)
    {
        const char c = *p;
        if (!((c >= 'A' && c <= 'Z') ||
              (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') ||
               c == '_' || c == '-' || c == '.'))
        {
            return false;
        }
    }
    return true;
}

#endif // EXECSCRIPT_IMPL_H
