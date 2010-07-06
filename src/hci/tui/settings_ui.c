/*
 * Copyright (C) 2006 Michael Brown <mbrown@fensystems.co.uk>.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

FILE_LICENCE ( GPL2_OR_LATER );

#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include <curses.h>
#include <console.h>
#include <gpxe/settings.h>
#include <gpxe/editbox.h>
#include <gpxe/keys.h>
#include <gpxe/settings_ui.h>

/** @file
 *
 * Option configuration console
 *
 */

/* Colour pairs */
#define CPAIR_NORMAL	1
#define CPAIR_SELECT	2
#define CPAIR_EDIT	3
#define CPAIR_ALERT	4

/* Screen layout */
#define TITLE_ROW		1
#define SETTINGS_LIST_ROW	3
#define SETTINGS_LIST_COL	1
#define SETTINGS_LIST_ROWS	16
#define INFO_ROW		20
#define ALERT_ROW		20
#define INSTRUCTION_ROW		22
#define INSTRUCTION_PAD "     "

/** Layout of text within a setting widget */
struct setting_row {
	char start[0];
	char pad1[1];
	char name[15];
	char pad2[1];
	char value[60];
	char pad3[1];
	char nul;
} __attribute__ (( packed ));

/** A setting widget */
struct setting_widget {
	/** Settings block */
	struct settings *settings;
	/** Total rows that can be displayed */
	unsigned int total_rows;
        /** Index of the first visible setting, for scrolling. */
	unsigned int first_visible;
	/** Configuration setting */
	struct setting *setting;
	/** Screen row */
	unsigned int row;
	/** Screen column */
	unsigned int col;
	/** Edit box widget used for editing setting */
	struct edit_box editbox;
	/** Editing in progress flag */
	int editing;
	/** Buffer for setting's value */
	char value[256]; /* enough size for a DHCP string */
};

/** Number of registered configuration settings */
#define NUM_SETTINGS table_num_entries ( SETTINGS )

static void load_setting ( struct setting_widget *widget ) __nonnull;
static int save_setting ( struct setting_widget *widget ) __nonnull;
static void init_widget ( struct setting_widget *widget,
                           struct settings *settings ) __nonnull;
static void draw_setting ( struct setting_widget *widget ) __nonnull;
static int edit_setting ( struct setting_widget *widget, int key ) __nonnull;
static void select_setting ( struct setting_widget *widget,
			     unsigned int index ) __nonnull;
static void reveal ( struct setting_widget *widget, unsigned int n) __nonnull;
static void vmsg ( unsigned int row, const char *fmt, va_list args ) __nonnull;
static void msg ( unsigned int row, const char *fmt, ... ) __nonnull;
static void valert ( const char *fmt, va_list args ) __nonnull;
static void alert ( const char *fmt, ... ) __nonnull;
static void draw_info_row ( struct setting *setting ) __nonnull;
static int main_loop ( struct settings *settings ) __nonnull;

/**
 * Load setting widget value from configuration settings
 *
 * @v widget		Setting widget
 *
 */
static void load_setting ( struct setting_widget *widget ) {

	/* Mark as not editing */
	widget->editing = 0;

	/* Read current setting value */
	if ( fetchf_setting ( widget->settings, widget->setting,
			      widget->value, sizeof ( widget->value ) ) < 0 ) {
		widget->value[0] = '\0';
	}	

	/* Initialise edit box */
	init_editbox ( &widget->editbox, widget->value,
		       sizeof ( widget->value ), NULL, widget->row,
		       ( widget->col + offsetof ( struct setting_row, value )),
		       sizeof ( ( ( struct setting_row * ) NULL )->value ), 0);
}

/**
 * Save setting widget value back to configuration settings
 *
 * @v widget		Setting widget
 */
static int save_setting ( struct setting_widget *widget ) {
	return storef_setting ( widget->settings, widget->setting,
				widget->value );
}

/**
 * Determine if a setting is relevant to the scope of a settings block.
 *
 * @v settings		Settings block that determines the scope
 * @v setting		Setting to test for relevance
 * @v recuse		Whether to consider child settings blocks
 * @ret relevant	Whether the setting is relevant to the settings block
 */
static int relevant ( struct settings *settings,
		      struct setting *setting ) {
	unsigned int relevant_type = TAG_TYPE ( settings->tag_magic );
	struct settings *child;

	if ( TAG_TYPE ( setting->tag ) == relevant_type )
		return 1;
	list_for_each_entry ( child, &settings->children, siblings ) {
		if ( relevant ( child, setting ) )
			return 1;
	}
	return 0;
}

/**
 * Lookup the n'th setting in the current scope.  If there is no n'th
 * setting in scope, return the number of settings in scope.
 *
 * @v settings		Settings structure that determines the scope
 * @v n			Index of the relevant setting
 * @ret setting		N'th relevant setting, else the relevant setting count
 */
static struct setting *relevant_setting ( struct settings *settings,
					  unsigned int n ) {
	struct setting *setting;
	unsigned int cnt = 0;

	for_each_table_entry ( setting, SETTINGS ) {
		if ( relevant ( settings, setting ) ) {
			if ( cnt++ == n )
				return setting;
		}
	}

	/* Return cnt to make relevant_setting_cnt() trivial. */
	return ( void * ) ( long ) cnt;
}

/**
 * Return the number of in-scope settings.
 *
 * @v settings		Settings structure determining the scope.
 * @ret cnt		Number of relevant (in-scope) settings.
 */

static unsigned int relevant_setting_cnt ( struct settings *settings ) {
	return (unsigned long) relevant_setting ( settings, UINT_MAX );
}

/**
 * Initialise the scrolling setting widget, drawing initial display.
 *
 * @v widget		Setting widget
 * @v settings		Settings block
 */
static void init_widget ( struct setting_widget *widget,
			  struct settings *settings ) {
	memset ( widget, 0, sizeof ( *widget ) );
	widget->settings = settings;
	widget->total_rows = relevant_setting_cnt ( settings );

	/* Draw all rows initially. */
	widget->first_visible = SETTINGS_LIST_ROWS;
	reveal ( widget, 0 );
}

/**
 * Draw setting widget
 *
 * @v widget		Setting widget
 */
static void draw_setting ( struct setting_widget *widget ) {
	struct setting_row row;
	unsigned int len;
	unsigned int curs_col;
	char *value;

	/* Fill row with spaces */
	memset ( &row, ' ', sizeof ( row ) );
	row.nul = '\0';

	/* Construct dot-padded name */
	memset ( row.name, '.', sizeof ( row.name ) );
	len = strlen ( widget->setting->name );
	if ( len > sizeof ( row.name ) )
		len = sizeof ( row.name );
	memcpy ( row.name, widget->setting->name, len );

	/* Construct space-padded value */
	value = widget->value;
	if ( ! *value )
		value = "<not specified>";
	len = strlen ( value );
	if ( len > sizeof ( row.value ) )
		len = sizeof ( row.value );
	memcpy ( row.value, value, len );
	curs_col = ( widget->col + offsetof ( typeof ( row ), value )
		     + len );

	/* Print row */
	mvprintw ( widget->row, widget->col, "%s", row.start );
	move ( widget->row, curs_col );
	if ( widget->editing )
		draw_editbox ( &widget->editbox );
}

/**
 * Edit setting widget
 *
 * @v widget		Setting widget
 * @v key		Key pressed by user
 * @ret key		Key returned to application, or zero
 */
static int edit_setting ( struct setting_widget *widget, int key ) {
	widget->editing = 1;
	return edit_editbox ( &widget->editbox, key );
}

/**
 * Select a setting for display updates, by index.
 *
 * @v widget		Setting widget
 * @v settings		Settings block
 * @v index		Index of setting with settings list
 */
static void select_setting ( struct setting_widget *widget,
			     unsigned int index ) {
	unsigned int skip = offsetof ( struct setting_widget, setting );

	/* Reset the widget, preserving static state. */
	memset ( ( char * ) widget + skip, 0, sizeof ( *widget ) - skip );
	widget->setting = relevant_setting ( widget->settings, index );
	widget->row = SETTINGS_LIST_ROW + index - widget->first_visible;
	widget->col = SETTINGS_LIST_COL;

	/* Read current setting value */
	load_setting ( widget );
}

/**
 * Print message centred on specified row
 *
 * @v row		Row
 * @v fmt		printf() format string
 * @v args		printf() argument list
 */
static void vmsg ( unsigned int row, const char *fmt, va_list args ) {
	char buf[COLS];
	size_t len;

	len = vsnprintf ( buf, sizeof ( buf ), fmt, args );
	mvprintw ( row, ( ( COLS - len ) / 2 ), "%s", buf );
}

/**
 * Print message centred on specified row
 *
 * @v row		Row
 * @v fmt		printf() format string
 * @v ..		printf() arguments
 */
static void msg ( unsigned int row, const char *fmt, ... ) {
	va_list args;

	va_start ( args, fmt );
	vmsg ( row, fmt, args );
	va_end ( args );
}

/**
 * Clear message on specified row
 *
 * @v row		Row
 */
static void clearmsg ( unsigned int row ) {
	move ( row, 0 );
	clrtoeol();
}

/**
 * Print alert message
 *
 * @v fmt		printf() format string
 * @v args		printf() argument list
 */
static void valert ( const char *fmt, va_list args ) {
	clearmsg ( ALERT_ROW );
	color_set ( CPAIR_ALERT, NULL );
	vmsg ( ALERT_ROW, fmt, args );
	sleep ( 2 );
	color_set ( CPAIR_NORMAL, NULL );
	clearmsg ( ALERT_ROW );
}

/**
 * Print alert message
 *
 * @v fmt		printf() format string
 * @v ...		printf() arguments
 */
static void alert ( const char *fmt, ... ) {
	va_list args;

	va_start ( args, fmt );
	valert ( fmt, args );
	va_end ( args );
}

/**
 * Draw title row
 */
static void draw_title_row ( void ) {
	attron ( A_BOLD );
	msg ( TITLE_ROW, "gPXE option configuration console" );
	attroff ( A_BOLD );
}

/**
 * Draw information row
 *
 * @v setting		Current configuration setting
 */
static void draw_info_row ( struct setting *setting ) {
	clearmsg ( INFO_ROW );
	attron ( A_BOLD );
	msg ( INFO_ROW, "%s - %s", setting->name, setting->description );
	attroff ( A_BOLD );
}

/**
 * Draw instruction row
 *
 * @v editing		Editing in progress flag
 */
static void draw_instruction_row ( int editing ) {
	clearmsg ( INSTRUCTION_ROW );
	if ( editing ) {
		msg ( INSTRUCTION_ROW,
		      "Enter - accept changes" INSTRUCTION_PAD
		      "Ctrl-C - discard changes" );
	} else {
		msg ( INSTRUCTION_ROW,
		      "Ctrl-D - delete setting" INSTRUCTION_PAD
		      "Ctrl-X - exit configuration utility" );
	}
}

/**
 * Reveal a setting by index: Scroll the setting list to reveal the
 * specified setting.
 *
 * @widget	The main loop's display widget.
 * @n		The index of the setting to reveal.
 */
static void reveal ( struct setting_widget *widget, unsigned int n)
{
	unsigned int i;

	/* Simply return if setting N is already on-screen. */
	if ( n - widget->first_visible < SETTINGS_LIST_ROWS )
		return;
	
	/* Jump scroll to make the specified setting visible. */
	while ( widget->first_visible < n )
		widget->first_visible += SETTINGS_LIST_ROWS;
	while ( widget->first_visible > n )
		widget->first_visible -= SETTINGS_LIST_ROWS;
	
	/* Draw elipses before and/or after the settings list to
	   represent any invisible settings. */
	mvaddstr ( SETTINGS_LIST_ROW - 1,
		   SETTINGS_LIST_COL + 1,
		   widget->first_visible > 0 ? "..." : "   " );
	mvaddstr ( SETTINGS_LIST_ROW + SETTINGS_LIST_ROWS,
		   SETTINGS_LIST_COL + 1,
		   ( (widget->first_visible + SETTINGS_LIST_ROWS
		      < widget->total_rows )
		     ? "..."
		     : "   " ) );
	
	/* Draw visible settings. */
	for ( i = 0; i < SETTINGS_LIST_ROWS; i++ ) {
		if ( widget->first_visible + i < widget->total_rows ) {
			select_setting ( widget, widget->first_visible + i );
			draw_setting ( widget );
		} else {
			clearmsg ( SETTINGS_LIST_ROW + i );
		}
	}

	/* Set the widget to the current row, which will be redrawn
	   appropriately by the main loop. */
	select_setting ( widget, n );
}

static int main_loop ( struct settings *settings ) {
	struct setting_widget widget;
	unsigned int current = 0;
	unsigned int next;
	int key;
	int rc;

	/* Print initial screen content */
	draw_title_row();
	color_set ( CPAIR_NORMAL, NULL );
	init_widget ( &widget, settings );
	
	while ( 1 ) {
		/* Redraw information and instruction rows */
		draw_info_row ( widget.setting );
		draw_instruction_row ( widget.editing );

		/* Redraw current setting */
		color_set ( ( widget.editing ? CPAIR_EDIT : CPAIR_SELECT ),
			    NULL );
		draw_setting ( &widget );
		color_set ( CPAIR_NORMAL, NULL );

		key = getkey();
		if ( widget.editing ) {
			key = edit_setting ( &widget, key );
			switch ( key ) {
			case CR:
			case LF:
				if ( ( rc = save_setting ( &widget ) ) != 0 ) {
					alert ( " Could not set %s: %s ",
						widget.setting->name,
						strerror ( rc ) );
				}
				/* Fall through */
			case CTRL_C:
				load_setting ( &widget );
				break;
			default:
				/* Do nothing */
				break;
			}
		} else {
			next = current;
			switch ( key ) {
			case KEY_DOWN:
				if ( next < widget.total_rows - 1 )
					reveal ( &widget, ++next );
				break;
			case KEY_UP:
				if ( next > 0 )
					reveal ( &widget, --next ) ;
				break;
			case CTRL_D:
				delete_setting ( widget.settings,
						 widget.setting );
				select_setting ( &widget, next );
				draw_setting ( &widget );
				break;
			case CTRL_X:
				return 0;
			default:
				edit_setting ( &widget, key );
				break;
			}	
			if ( next != current ) {
				draw_setting ( &widget );
				select_setting ( &widget, next );
				current = next;
			}
		}
	}
	
}

int settings_ui ( struct settings *settings ) {
	int rc;

	initscr();
	start_color();
	init_pair ( CPAIR_NORMAL, COLOR_WHITE, COLOR_BLUE );
	init_pair ( CPAIR_SELECT, COLOR_WHITE, COLOR_RED );
	init_pair ( CPAIR_EDIT, COLOR_BLACK, COLOR_CYAN );
	init_pair ( CPAIR_ALERT, COLOR_WHITE, COLOR_RED );
	color_set ( CPAIR_NORMAL, NULL );
	erase();
	
	rc = main_loop ( settings );

	endwin();

	return rc;
}
