#include "video.h"

_VIDEO_API_ VideoSettings video;

VideoSettings::VideoSettings()
: render(false), useMasking(false), useEnvMapping(true), xRes(0), yRes(0), fov(45.0f)
{
}
