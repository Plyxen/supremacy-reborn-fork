#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// Lightweight debug logger + crash handler.
// Log file:  Documents\user\debug.log
// Crash dump: Documents\user\crash.dmp (on unhandled exception)
// ─────────────────────────────────────────────────────────────────────────────

namespace dbg {

    inline std::string GetLogPath( ) {
        char path[ MAX_PATH ];
        if( SUCCEEDED( SHGetFolderPathA( nullptr, CSIDL_PERSONAL, nullptr, SHGFP_TYPE_CURRENT, path ) ) )
            return std::string( path ) + "\\user\\debug.log";
        return "debug.log";
    }

    inline void Log( const char* category, const char* msg ) {
        std::ofstream f( GetLogPath( ), std::ios::app );
        if( !f ) return;

        time_t t = std::time( nullptr );
        struct tm tm_info;
        localtime_s( &tm_info, &t );
        char timebuf[ 16 ];
        std::strftime( timebuf, sizeof( timebuf ), "%H:%M:%S", &tm_info );

        f << "[" << timebuf << "] [" << category << "] " << msg << "\n";
    }

    // printf-style variant.
    template< typename... Args >
    inline void Logf( const char* category, const char* fmt, Args... args ) {
        char buf[ 512 ];
        sprintf_s( buf, fmt, args... );
        Log( category, buf );
    }

    // Unhandled exception filter — writes exception info to the log file.
    inline LONG WINAPI CrashHandler( EXCEPTION_POINTERS* ep ) {
        char msg[ 256 ];
        sprintf_s( msg,
            "Unhandled exception 0x%08X at 0x%p  "
            "(Documents\\user\\debug.log for context)",
            ep->ExceptionRecord->ExceptionCode,
            ep->ExceptionRecord->ExceptionAddress );
        Log( "CRASH", msg );
        return EXCEPTION_CONTINUE_SEARCH;
    }

    // Call once at DLL attach.
    inline void Install( ) {
        SetUnhandledExceptionFilter( CrashHandler );
        Log( "INIT", "cheat loaded — debug logger active" );
    }

} // namespace dbg
