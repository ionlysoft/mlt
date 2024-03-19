#include <string.h>
#include <limits.h>
#include <framework/mlt.h>


extern mlt_filter filter_seetaface_init( mlt_profile profile, mlt_service_type type, const char *id, char *arg );

static mlt_properties metadata( mlt_service_type type, const char *id, void *data )
{
	char file[ PATH_MAX ];
	snprintf( file, PATH_MAX, "%s/plus/%s", mlt_environment( "MLT_DATA" ), (char*) data );
	return mlt_properties_parse_yaml( file );
}

//MLT_REPOSITORY
void mlt_register_seetaface(mlt_repository repository)
{
    MLT_REGISTER( mlt_service_filter_type, "seetaface", filter_seetaface_init );

    MLT_REGISTER_METADATA( mlt_service_filter_type, "seetaface", metadata, "filter_seetaface.yml" );
}
