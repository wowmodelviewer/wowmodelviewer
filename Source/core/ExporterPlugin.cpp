#include "ExporterPlugin.h"
#include <string>
#include <QtGui/QImage>

void ExporterPlugin::exportGLTexture(GLuint id, std::wstring filename) const
{
	LOG_INFO << "Exporting GL texture with id " << id << "in" << filename.c_str();
	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, id);

	GLint width, height;
	glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
	glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &height);

	unsigned char* pixels = new unsigned char[width * height * 4];

	glGetTexImage(GL_TEXTURE_2D, 0, GL_BGRA, GL_UNSIGNED_BYTE, pixels);

	const QImage texture(pixels, width, height, QImage::Format_ARGB32);
	texture.save(QString::fromStdWString(filename));

	glBindTexture(GL_TEXTURE_2D, 0);
	glDisable(GL_TEXTURE_2D);
}
