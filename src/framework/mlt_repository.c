﻿/**
 * \file mlt_repository.c
 * \brief provides a map between service and shared objects
 * \see mlt_repository_s
 *
 * Copyright (C) 2003-2014 Meltytech, LLC
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "mlt_repository.h"
#include "mlt_properties.h"
#include "mlt_tokeniser.h"
#include "mlt_log.h"
#include "mlt_factory.h"

#include <stdio.h>
#include <stdlib.h>
#include <w32dlfcn.h>
//#include <dlfcn.h>
#include <string.h>
#include <limits.h>
#include <dirent.h>
#include <sys/stat.h>

/** \brief Repository class
 *
 * The Repository is a collection of plugin modules and their services and service metadata.
 *
 * \extends mlt_properties_s
 * \properties \p language a cached list of user locales
 */

struct mlt_repository_s
{
	struct mlt_properties_s parent; /// a list of object files
	mlt_properties consumers;       /// a list of entry points for consumers
	mlt_properties filters;         /// a list of entry points for filters
	mlt_properties links;           /// a list of entry points for links
	mlt_properties producers;       /// a list of entry points for producers
	mlt_properties transitions;     /// a list of entry points for transitions
};

/** Construct a new repository.
 *
 * \public \memberof mlt_repository_s
 * \param directory the full path of a directory from which to read modules
 * \return a new repository or NULL if failed
 */
extern void mlt_register_ffmpeg( mlt_repository repository);
extern void mlt_register_core( mlt_repository repository);
extern void mlt_register_sdl2( mlt_repository repository);
extern void mlt_register_xml( mlt_repository repository);
extern void mlt_register_plus( mlt_repository repository);
//extern void mlt_register_rtaudio( mlt_repository repository);
//extern void mlt_register_opengl(mlt_repository repository);
extern void mlt_register_qt(mlt_repository repository);
//extern void mlt_register_seetaface(mlt_repository repository);
extern void mlt_register_normalize( mlt_repository repository);
extern void mlt_register_oldfilm(mlt_repository repository);

mlt_repository mlt_repository_init( const char *directory )
{
		// Safety check
	if ( directory == NULL || strcmp( directory, "" ) == 0 )
		return NULL;

    // Construct the repository
    mlt_repository self = calloc( 1, sizeof( struct mlt_repository_s ));
    mlt_properties_init( &self->parent, self );
    self->consumers = mlt_properties_new();
    self->filters = mlt_properties_new();
    self->links = mlt_properties_new();
    self->producers = mlt_properties_new();
    self->transitions = mlt_properties_new();

    int plugin_count = 0;
    // Get the registration function
    mlt_repository_callback symbol_ptr = mlt_register_ffmpeg;

    if ( symbol_ptr != NULL )
    {
        symbol_ptr( self );
        mlt_properties_set_data( &self->parent, "libmltavformat", NULL, 0, NULL, NULL );
        ++plugin_count;
    }

    symbol_ptr = mlt_register_core;

    if ( symbol_ptr != NULL )
    {
        symbol_ptr( self );
        mlt_properties_set_data( &self->parent, "libmltcore", NULL, 0, NULL, NULL );
        ++plugin_count;
    }

    symbol_ptr = mlt_register_sdl2;

    if ( symbol_ptr != NULL )
    {
        symbol_ptr( self );
        mlt_properties_set_data( &self->parent, "libmltsdl2", NULL, 0, NULL, NULL );
        ++plugin_count;
    }

    symbol_ptr = mlt_register_xml;

    if ( symbol_ptr != NULL )
    {
        symbol_ptr( self );
        mlt_properties_set_data( &self->parent, "libmltxml", NULL, 0, NULL, NULL );
        ++plugin_count;
    }

    symbol_ptr = mlt_register_plus;

    if ( symbol_ptr != NULL )
    {
        symbol_ptr( self );
        mlt_properties_set_data( &self->parent, "libmltplus", NULL, 0, NULL, NULL );
        ++plugin_count;
    }


    symbol_ptr = mlt_register_qt;

    if ( symbol_ptr != NULL )
    {
        symbol_ptr( self );
        mlt_properties_set_data( &self->parent, "libmltqt", NULL, 0, NULL, NULL );
        ++plugin_count;
    }

    /*symbol_ptr = mlt_register_rtaudio;
    if ( symbol_ptr != NULL )
    {
        symbol_ptr( self );
        mlt_properties_set_data( &self->parent, "libmltrtaudio", NULL, 0, NULL, NULL );
        ++plugin_count;
    }
    */

    /*
    symbol_ptr = mlt_register_opengl;
    if ( symbol_ptr != NULL )
    {
        symbol_ptr( self );
        mlt_properties_set_data( &self->parent, "libmltopengl", NULL, 0, NULL, NULL );
        ++plugin_count;
    }
    */

    /*symbol_ptr = mlt_register_seetaface;
    if ( symbol_ptr != NULL )
    {
        symbol_ptr( self );
        mlt_properties_set_data( &self->parent, "libmltseetaface", NULL, 0, NULL, NULL );
        ++plugin_count;
    }*/

    symbol_ptr = mlt_register_normalize;

    if ( symbol_ptr != NULL )
    {
        symbol_ptr( self );
        mlt_properties_set_data( &self->parent, "libmltnormalize", NULL, 0, NULL, NULL );
        ++plugin_count;
    }

    symbol_ptr = mlt_register_oldfilm;

    if ( symbol_ptr != NULL )
    {
        symbol_ptr( self );
        mlt_properties_set_data( &self->parent, "libmltoldfilm", NULL, 0, NULL, NULL );
        ++plugin_count;
    }


    return self;
}

/** Create a properties list for a service holding a function pointer to its constructor function.
 *
 * \private \memberof mlt_repository_s
 * \param symbol a pointer to a function that can create the service.
 * \return a properties list
 */

static mlt_properties new_service( void *symbol )
{
	mlt_properties properties = mlt_properties_new();
	mlt_properties_set_data( properties, "symbol", symbol, 0, NULL, NULL );
	return properties;
}

/** Register a service with the repository.
 *
 * Typically, this is invoked by a module within its mlt_register().
 *
 * \public \memberof mlt_repository_s
 * \param self a repository
 * \param service_type a service class
 * \param service the name of a service
 * \param symbol a pointer to a function to create the service
 */

void mlt_repository_register( mlt_repository self, mlt_service_type service_type, const char *service, mlt_register_callback symbol )
{
	// Add the entry point to the corresponding service list
	switch ( service_type )
	{
		case mlt_service_consumer_type:
			mlt_properties_set_data( self->consumers, service, new_service( symbol ), 0, ( mlt_destructor )mlt_properties_close, NULL );
			break;
		case mlt_service_filter_type:
			mlt_properties_set_data( self->filters, service, new_service( symbol ), 0, ( mlt_destructor )mlt_properties_close, NULL );
			break;
		case mlt_service_link_type:
			mlt_properties_set_data( self->links, service, new_service( symbol ), 0, ( mlt_destructor )mlt_properties_close, NULL );
			break;
		case mlt_service_producer_type:
			mlt_properties_set_data( self->producers, service, new_service( symbol ), 0, ( mlt_destructor )mlt_properties_close, NULL );
			break;
		case mlt_service_transition_type:
			mlt_properties_set_data( self->transitions, service, new_service( symbol ), 0, ( mlt_destructor )mlt_properties_close, NULL );
			break;
		default:
			mlt_log_error( NULL, "%s: Unable to register \"%s\"\n", __FUNCTION__, service );
			break;
	}
}

/** Get the repository properties for particular service class.
 *
 * \private \memberof mlt_repository_s
 * \param self a repository
 * \param type a service class
 * \param service the name of a service
 * \return a properties list or NULL if error
 */

static mlt_properties get_service_properties( mlt_repository self, mlt_service_type type, const char *service )
{
	mlt_properties service_properties = NULL;

	// Get the entry point from the corresponding service list
	switch ( type )
	{
		case mlt_service_consumer_type:
			service_properties = mlt_properties_get_data( self->consumers, service, NULL );
			break;
		case mlt_service_filter_type:
			service_properties = mlt_properties_get_data( self->filters, service, NULL );
			break;
		case mlt_service_link_type:
			service_properties = mlt_properties_get_data( self->links, service, NULL );
			break;
		case mlt_service_producer_type:
			service_properties = mlt_properties_get_data( self->producers, service, NULL );
			break;
		case mlt_service_transition_type:
			service_properties = mlt_properties_get_data( self->transitions, service, NULL );
			break;
		default:
			break;
	}
	return service_properties;
}

/** Construct a new instance of a service.
 *
 * \public \memberof mlt_repository_s
 * \param self a repository
 * \param profile a \p mlt_profile to give the service
 * \param type a service class
 * \param service the name of the service
 * \param input an optional argument to the service constructor
 */

void *mlt_repository_create( mlt_repository self, mlt_profile profile, mlt_service_type type, const char *service, const void *input )
{
	mlt_properties properties = get_service_properties( self, type, service );
	if ( properties != NULL )
	{
		mlt_register_callback symbol_ptr = mlt_properties_get_data( properties, "symbol", NULL );

		// Construct the service
		return ( symbol_ptr != NULL ) ? symbol_ptr( profile, type, service, input ) : NULL;
	}
	return NULL;
}

/** Destroy a repository and free its resources.
 *
 * \public \memberof mlt_repository_s
 * \param self a repository
 */

void mlt_repository_close( mlt_repository self )
{
	mlt_properties_close( self->consumers );
	mlt_properties_close( self->filters );
	mlt_properties_close( self->producers );
	mlt_properties_close( self->transitions );
	mlt_properties_close( &self->parent );
	free( self );
}

/** Get the list of registered consumers.
 *
 * \public \memberof mlt_repository_s
 * \param self a repository
 * \return a properties list containing all of the consumers
 */

MLTPP_DECLSPEC mlt_properties mlt_repository_consumers( mlt_repository self )
{
	return self->consumers;
}

/** Get the list of registered filters.
 *
 * \public \memberof mlt_repository_s
 * \param self a repository
 * \return a properties list of all of the filters
 */

MLTPP_DECLSPEC mlt_properties mlt_repository_filters( mlt_repository self )
{
	return self->filters;
}

/** Get the list of registered links.
 *
 * \public \memberof mlt_repository_s
 * \param self a repository
 * \return a properties list of all of the links
 */

mlt_properties mlt_repository_links( mlt_repository self )
{
	return self->links;
}

/** Get the list of registered producers.
 *
 * \public \memberof mlt_repository_s
 * \param self a repository
 * \return a properties list of all of the producers
 */

MLTPP_DECLSPEC mlt_properties mlt_repository_producers( mlt_repository self )
{
	return self->producers;
}

/** Get the list of registered transitions.
 *
 * \public \memberof mlt_repository_s
 * \param self a repository
 * \return a properties list of all of the transitions
 */

MLTPP_DECLSPEC mlt_properties mlt_repository_transitions( mlt_repository self )
{
	return self->transitions;
}

/** Register the metadata for a service.
 *
 * IMPORTANT: mlt_repository will take responsibility for deallocating the metadata properties
 * that you supply!
 *
 * \public \memberof mlt_repository_s
 * \param self a repository
 * \param type a service class
 * \param service the name of a service
 * \param callback the pointer to a function that can supply metadata
 * \param callback_data an opaque user data pointer to be supplied on the callback
 */

void mlt_repository_register_metadata( mlt_repository self, mlt_service_type type, const char *service, mlt_metadata_callback callback, void *callback_data )
{
	mlt_properties service_properties = get_service_properties( self, type, service );
	mlt_properties_set_data( service_properties, "metadata_cb", callback, 0, NULL, NULL );
	mlt_properties_set_data( service_properties, "metadata_cb_data", callback_data, 0, NULL, NULL );
}

/** Get the metadata about a service.
 *
 * Returns NULL if service or its metadata are unavailable.
 *
 * \public \memberof mlt_repository_s
 * \param self a repository
 * \param type a service class
 * \param service the name of a service
 * \return the service metadata as a structured properties list
 */

MLTPP_DECLSPEC mlt_properties mlt_repository_metadata( mlt_repository self, mlt_service_type type, const char *service )
{
	mlt_properties metadata = NULL;
	mlt_properties properties = get_service_properties( self, type, service );

	// If this is a valid service
	if ( properties )
	{
		// Lookup cached metadata
		metadata = mlt_properties_get_data( properties, "metadata", NULL );
		if ( ! metadata )
		{
			// Not cached, so get the registered metadata callback function
			mlt_metadata_callback callback = mlt_properties_get_data( properties, "metadata_cb", NULL );

			// If a metadata callback function is registered
			if ( callback )
			{
				// Fetch the callback data arg
				void *data = mlt_properties_get_data( properties, "metadata_cb_data", NULL );

				// Fetch the metadata through the callback
				metadata = callback( type, service, data );

				// Cache the metadata
				if ( metadata )
					// Include dellocation and serialisation
					mlt_properties_set_data( properties, "metadata", metadata, 0, ( mlt_destructor )mlt_properties_close, ( mlt_serialiser )mlt_properties_serialise_yaml );
			}
		}
	}
	return metadata;
}

/** Try to determine the locale from some commonly used environment variables.
 *
 * \private \memberof mlt_repository_s
 * \return a string containing the locale id or NULL if unknown
 */

static char *getenv_locale()
{
	char *s = getenv( "LANGUAGE" );
	if ( s && s[0] )
		return s;
	s = getenv( "LC_ALL" );
	if ( s && s[0] )
		return s;
	s = getenv( "LC_MESSAGES" );
	if ( s && s[0] )
		return s;
	s = getenv( "LANG" );
	if ( s && s[0] )
		return s;
	return NULL;
}

/** Return a list of user-preferred language codes taken from environment variables.
 *
 * A module should use this to locate a localized YAML Tiny file from which to build
 * its metadata strucutured properties.
 *
 * \public \memberof mlt_repository_s
 * \param self a repository
 * \return a properties list that is a list (not a map) of locales, defaults to "en" if not
 * overridden by environment variables, in order: LANGUAGE, LC_ALL, LC_MESSAGES, LANG
 */

mlt_properties mlt_repository_languages( mlt_repository self )
{
	mlt_properties languages = mlt_properties_get_data( &self->parent, "languages", NULL );
	if ( languages )
		return languages;

	languages = mlt_properties_new();
	char *locale = getenv_locale();
	if ( locale )
	{
		locale = strdup( locale );
		mlt_tokeniser tokeniser = mlt_tokeniser_init();
		int count = mlt_tokeniser_parse_new( tokeniser, locale, ":" );
		if ( count )
		{
			int i;
			for ( i = 0; i < count; i++ )
			{
				char *locale = mlt_tokeniser_get_string( tokeniser, i );
				if ( strcmp( locale, "C" ) == 0 || strcmp( locale, "POSIX" ) == 0 )
					locale = "en";
				else if ( strlen( locale ) > 2 )
					locale[2] = 0;
				char string[21];
				snprintf( string, sizeof(string), "%d", i );
				mlt_properties_set( languages, string, locale );
			}
		}
		else
		{
			mlt_properties_set( languages, "0", "en" );
		}
		free( locale );
		mlt_tokeniser_close( tokeniser );
	}
	else
	{
		mlt_properties_set( languages, "0", "en" );
	}
	mlt_properties_set_data( &self->parent, "languages", languages, 0, ( mlt_destructor )mlt_properties_close, NULL );
	return languages;
}

static void list_presets( mlt_properties properties, const char *path, const char *dirname )
{
	DIR *dir = opendir( dirname );

	if ( dir )
	{
		struct dirent *de = readdir( dir );
		char fullname[ PATH_MAX ];

		while ( de != NULL )
		{
			if ( de->d_name[0] != '.' && de->d_name[strlen( de->d_name ) - 1] != '~' )
			{
				struct stat info;

				snprintf( fullname, sizeof(fullname), "%s/%s", dirname, de->d_name );
				stat( fullname, &info );
				if ( S_ISDIR( info.st_mode ) )
				{
					// recurse into subdirectories
					char sub[ PATH_MAX ];
					if ( path )
						snprintf( sub, sizeof(sub), "%s/%s", path, de->d_name );
					else
						strncpy( sub, de->d_name, sizeof(sub) );
					list_presets( properties, sub, fullname );
				}
				else
				{
					// load the preset
					mlt_properties preset = mlt_properties_load( fullname );
					if ( preset && mlt_properties_count( preset ) )
					{
						snprintf( fullname, 1024, "%s/%s", path, de->d_name );
						mlt_properties_set_data( properties, fullname, preset, 0, (mlt_destructor) mlt_properties_close, NULL );
					}
				}
			}
			de = readdir( dir );
		}
		closedir( dir );
	}
}

/** Get the list of presets.
 *
 * \public \memberof mlt_repository_s
 * \return a properties list of all the presets
 */

MLTPP_DECLSPEC mlt_properties mlt_repository_presets( )
{
	mlt_properties result = mlt_properties_new();
	list_presets( result, NULL, mlt_environment( "MLT_PRESETS_PATH" ) );
	return result;
}
