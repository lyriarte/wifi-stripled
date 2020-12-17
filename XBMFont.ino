#include "XBMFont.h"


XBMFont::XBMFont(unsigned int width, unsigned int height, unsigned char ** chars) {
	this->width = width;
	this->height = height;
	this->chars = chars; 
}


unsigned int XBMFont::getWidth() {
	return this->width;
}


unsigned int XBMFont::getHeight() {
	return this->height;
}


unsigned char * XBMFont::getBitmap(char c) {
	unsigned int i = (unsigned int) c;
	if (i < XBMFont_min_printable || i > XBMFont_max_printable)
		i = XBMFont_max_printable;
	return this->chars[i - XBMFont_min_printable];
}


unsigned char XBMFont::getLine(char c, unsigned int line) {
	if (line >= this->height)
		line = this->height -1;
	return this->getBitmap(c)[line];
}


bool XBMFont::getPixel(char c, unsigned int line, unsigned int column) {
	if (column >= this->width)
		column = 0;
	unsigned char bits = this->getLine(c,line);
	return bits & (1 << column);
}



