#pragma once

void awl_log_init( int level );
int awl_log_has_init( void );
void awl_log_destroy( void );

void awl_log_printer_( const char* logname, int loglevel, const char* fname, int line, const char* fmt, ... );

#define awl_err_printf( ... ) awl_log_printer_( "error", 0, __FILE__, __LINE__, __VA_ARGS__ );
#define awl_wrn_printf( ... ) awl_log_printer_( "warn", 1, __FILE__, __LINE__, __VA_ARGS__ );
#define awl_log_printf( ... ) awl_log_printer_( "log", 2, __FILE__, __LINE__, __VA_ARGS__ );
#define awl_vrb_printf( ... ) awl_log_printer_( "verbose", 3, __FILE__, __LINE__, __VA_ARGS__ );

