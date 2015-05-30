/*
 * Another World engine rewrite
 * Copyright (C) 2004-2005 Gregory Montoir (cyx@users.sourceforge.net)
 */

#include "video.h"
#include "bitmap.h"
#include "resource.h"
#include "systemstub.h"


Video::Video(Resource *res, SystemStub *stub) 
	: _res(res), _stub(stub), _hasHeadSprites(false), _displayHead(true) {
}

void Video::init() {
	_nextPal = _currentPal = 0xFF;
	_listPtrs[2] = getPagePtr(1);
	_listPtrs[1] = getPagePtr(2);
	setWorkPagePtr(0xFE);
	_pData.byteSwap = (_res->getDataType() == Resource::DT_3DO);
}

void Video::setDefaultFont() {
	_stub->setFont(0, 0, 0);
}

void Video::setFont(const uint8_t *font) {
	int w, h;
	uint8_t *buf = decode_bitmap(font, true, -1, &w, &h);
	if (buf) {
		_stub->setFont(buf, w, h);
		free(buf);
	}
}

void Video::setHeads(const uint8_t *src) {
	int w, h;
	uint8_t *buf = decode_bitmap(src, true, 0xF06080, &w, &h);
	if (buf) {
		_stub->setSpriteAtlas(buf, w, h, 2, 2);
		free(buf);
		_hasHeadSprites = true;
	}
}

void Video::setDataBuffer(uint8_t *dataBuf, uint16_t offset) {
	_dataBuf = dataBuf;
	_pData.pc = dataBuf + offset;
}

void Video::drawShape(uint8_t color, uint16_t zoom, const Point *pt) {
	uint8_t i = _pData.fetchByte();
	if (i >= 0xC0) {
		if (color & 0x80) {
			color = i & 0x3F;
		}
		fillPolygon(color, zoom, pt);
	} else {
		i &= 0x3F;
		if (i == 1) {
			warning("Video::drawShape() ec=0x%X (i != 2)", 0xF80);
		} else if (i == 2) {
			drawShapeParts(zoom, pt);
		} else {
			warning("Video::drawShape() ec=0x%X (i != 2)", 0xFBB);
		}
	}
}

void Video::drawShapePart3DO(int color, int part, const Point *pt) {
	const uint8_t *vertices = _vertices3DO[part];
	const int w = *vertices++;
	const int h = *vertices++;
	const int x = pt->x - w / 2;
	const int y = pt->y - h / 2;
	QuadStrip qs;
	qs.numVertices = 2 * h;
	for (int i = 0; i < h; ++i) {
		qs.vertices[i].x = x + *vertices++;
		qs.vertices[i].y = y + i;
		qs.vertices[2 * h - 1 - i].x = x + *vertices++;
		qs.vertices[2 * h - 1 - i].y = y + i;
	}
	_stub->addQuadStripToList(_listPtrs[0], color, &qs);
}

void Video::drawShape3DO(int color, int zoom, const Point *pt) {
	const int code = _pData.fetchByte();
	debug(DBG_VIDEO, "Video::drawShape3DO() code=0x%x pt=%d,%d", code, pt->x, pt->y);
	if (color == 0xFF) {
		color = code & 31;
	}
	switch (code & 0xE0) {
	case 0x00: {
			const int x0 = pt->x - _pData.fetchByte() * zoom / 64;
			const int y0 = pt->y - _pData.fetchByte() * zoom / 64;
			int count = _pData.fetchByte() + 1;
			do {
				uint16_t offset = _pData.fetchWord();
				Point po;
				po.x = x0 + _pData.fetchByte() * zoom / 64;
				po.y = y0 + _pData.fetchByte() * zoom / 64;
				color = 0xFF;
				if (offset & 0x8000) {
					color = _pData.fetchByte();
					int part = _pData.fetchByte();
					if (color & 0x80) {
						color &= 0xF;
						drawShapePart3DO(color, part, &po);
						continue;
					}
					offset &= 0x7FFF;
				}
				uint8_t *bak = _pData.pc;
				_pData.pc = _dataBuf + offset * 2;
				drawShape3DO(color, zoom, &po);
				_pData.pc = bak;
			} while (--count != 0);
		}
		break;
	case 0x20: { // rect
			const int w = _pData.fetchByte() * zoom / 64;
			const int h = _pData.fetchByte() * zoom / 64;
			const int x1 = pt->x - w / 2;
			const int y1 = pt->y - h / 2;
			const int x2 = x1 + w;
			const int y2 = y1 + h;
			if (x1 > 319 || x2 < 0 || y1 > 199 || y2 < 0) {
				break;
			}
			QuadStrip qs;
			qs.numVertices = 4;
			qs.vertices[0].x = x1;
			qs.vertices[0].y = y1;
			qs.vertices[1].x = x1;
			qs.vertices[1].y = y2;
			qs.vertices[2].x = x2;
			qs.vertices[2].y = y2;
			qs.vertices[3].x = x2;
			qs.vertices[3].y = y1;
			_stub->addQuadStripToList(_listPtrs[0], color, &qs);
		}
		break;
	case 0x40: { // pixel
			if (pt->x > 319 || pt->x < 0 || pt->y > 199 || pt->y < 0) {
				break;
			}
			_stub->addPointToList(_listPtrs[0], color, pt);
		}
		break;
	case 0xC0: { // polygon
			const int w = _pData.fetchByte() * zoom / 64;
			const int h = _pData.fetchByte() * zoom / 64;
			const int count = _pData.fetchByte();
			QuadStrip qs;
			qs.numVertices = count * 2;
			assert(qs.numVertices < QuadStrip::MAX_VERTICES);
			const int x0 = pt->x - w / 2;
			const int y0 = pt->y - h / 2;
			if (x0 > 319 || pt->x + w / 2 < 0 || y0 > 199 || pt->y + h / 2 < 0) {
				break;
			}
			for (int i = 0, j = count * 2 - 1; i < count; ++i, --j) {
				const int x1 = _pData.fetchByte() * zoom / 64;
				const int x2 = _pData.fetchByte() * zoom / 64;
				const int y  = _pData.fetchByte() * zoom / 64;
				qs.vertices[i].x = x0 + x2;
				qs.vertices[(i + 1) % count].y = y0 + y;
				qs.vertices[j].x = x0 + x1;
				qs.vertices[count *  2 - 1 - (i + 1) % count].y = y0 + y;
			}
			_stub->addQuadStripToList(_listPtrs[0], color, &qs);
		}
		break;
	default:
		warning("Video::drawShape3DO() unhandled code 0x%X", code);
		break;
	}
}

void Video::fillPolygon(uint16_t color, uint16_t zoom, const Point *pt) {
	const uint8_t *p = _pData.pc;

	uint16_t bbw = (*p++) * zoom / 64;
	uint16_t bbh = (*p++) * zoom / 64;
	
	int16_t x1 = pt->x - bbw / 2;
	int16_t x2 = pt->x + bbw / 2;
	int16_t y1 = pt->y - bbh / 2;
	int16_t y2 = pt->y + bbh / 2;

	if (x1 > 319 || x2 < 0 || y1 > 199 || y2 < 0)
		return;

	QuadStrip qs;
	qs.numVertices = *p++;
	if ((qs.numVertices & 1) != 0) {
		warning("Unexpected number of vertices %d", qs.numVertices);
		return;
	}
	assert(qs.numVertices < QuadStrip::MAX_VERTICES);

	for (int i = 0; i < qs.numVertices; ++i) {
		Point *v = &qs.vertices[i];
		v->x = x1 + (*p++) * zoom / 64;
		v->y = y1 + (*p++) * zoom / 64;
	}

	if (qs.numVertices == 4 && bbw == 0 && bbh <= 1) {
		_stub->addPointToList(_listPtrs[0], color, pt);
	} else {
		_stub->addQuadStripToList(_listPtrs[0], color, &qs);
	}
}

void Video::drawShapeParts(uint16_t zoom, const Point *pgc) {
	Point pt;
	pt.x = pgc->x - _pData.fetchByte() * zoom / 64;
	pt.y = pgc->y - _pData.fetchByte() * zoom / 64;
	int16_t n = _pData.fetchByte();
	debug(DBG_VIDEO, "Video::drawShapeParts n=%d", n);
	for ( ; n >= 0; --n) {
		uint16_t off = _pData.fetchWord();
		Point po(pt);
		po.x += _pData.fetchByte() * zoom / 64;
		po.y += _pData.fetchByte() * zoom / 64;
		uint16_t color = 0xFF;
		if (off & 0x8000) {
			color = *_pData.pc & 0x7F;
			if (_hasHeadSprites && _stub->getRenderMode() != RENDER_ORIGINAL && _displayHead) {
				const int id = _pData.pc[1];
				switch (id) {
				case 0x4A: { // facing right
						Point pos(po.x - 4, po.y - 7);
						_stub->addSpriteToList(_listPtrs[0], 0, &pos);
					}
				case 0x4D:
					return;
				case 0x4F: { // facing left
						Point pos(po.x - 4, po.y - 7);
						_stub->addSpriteToList(_listPtrs[0], 1, &pos);
					}
				case 0x50:
					return;
				}
			}
			_pData.pc += 2;
		}
		off &= 0x7FFF;
		uint8_t *bak = _pData.pc;
		_pData.pc = _dataBuf + off * 2;
		drawShape(color, zoom, &po);
		_pData.pc = bak;
	}
}

static const int NTH_EDITION_STRINGS_COUNT = 157;

static const char *findString15th(int id) {
	for (int i = 0; i < NTH_EDITION_STRINGS_COUNT; ++i) {
		if (Video::_stringsId15th[i] == id) {
			return Video::_stringsTable15th[i];
		}
	}
	return 0;
}

static const char *findString20th(Resource *res, int id) {
	for (int i = 0; i < NTH_EDITION_STRINGS_COUNT; ++i) {
		if (Video::_stringsId15th[i] == id) {
			return res->getString(i);
		}
	}
	return 0;
}

static const char *findString(const StrEntry *stringsTable, int id) {
	for (const StrEntry *se = stringsTable; se->id != 0xFFFF; ++se) {
		if (se->id == id) {
			return se->str;
		}
	}
	return 0;
}

void Video::drawString(uint8_t color, uint16_t x, uint16_t y, uint16_t strId) {
	bool escapedChars = false;
	const char *str;
	if (_res->getDataType() == Resource::DT_15TH_EDITION) {
		str = findString15th(strId);
	} else if (_res->getDataType() == Resource::DT_20TH_EDITION) {
		str = findString20th(_res, strId);
		escapedChars = true;
	} else if (_res->getDataType() == Resource::DT_WIN31) {
		str = _res->getString(strId);
	} else if (_res->getDataType() == Resource::DT_3DO) {
		str = findString(_stringsTable3DO, strId);
	} else {
		str = findString(_stringsTable, strId);
	}
	if (!str) {
		warning("Unknown string id 0x%x", strId);
		return;
	}
	debug(DBG_VIDEO, "drawString(%d, %d, %d, '%s')", color, x, y, str);
	uint16_t xx = x;
	int len = strlen(str);
	for (int i = 0; i < len; ++i) {
		if (str[i] == '\n') {
			y += 8;
			x = xx;
		} else if (str[i] == '\\' && escapedChars) {
			++i;
			if (i < len) {
				switch (str[i]) {
				case 'n':
					y += 8;
					x = xx;
					break;
				}
			}
		} else {
			Point pt(x * 8, y);
			_stub->addCharToList(_listPtrs[0], color, str[i], &pt);
			++x;
		}
	}
}

uint8_t Video::getPagePtr(uint8_t page) {
	uint8_t p;
	if (page <= 3) {
		p = page;
	} else {
		switch (page) {
		case 0xFF:
			p = _listPtrs[2];
			break;
		case 0xFE:
			p = _listPtrs[1];
			break;
		default:
			p = 0; // XXX check
			warning("Video::getPagePtr() p != [0,1,2,3,0xFF,0xFE] == 0x%X", page);
			break;
		}
	}
	return p;
}

void Video::setWorkPagePtr(uint8_t page) {
	debug(DBG_VIDEO, "Video::setWorkPagePtr(%d)", page);
	_listPtrs[0] = getPagePtr(page);
}

void Video::fillPage(uint8_t page, uint8_t color) {
	debug(DBG_VIDEO, "Video::fillPage(%d, %d)", page, color);
	_stub->clearList(getPagePtr(page), color);
}

void Video::copyPage(uint8_t src, uint8_t dst, int16_t vscroll) {
	debug(DBG_VIDEO, "Video::copyPage(%d, %d)", src, dst);
	if (src >= 0xFE || !((src &= 0xBF) & 0x80)) {
		_stub->copyList(getPagePtr(dst), getPagePtr(src));
	} else {
		uint8_t sl = getPagePtr(src & 3);
		uint8_t dl = getPagePtr(dst);
		if (sl != dl && vscroll >= -199 && vscroll <= 199) {
			_stub->copyList(dl, sl, vscroll);
		}
	}
}

static void decode_amiga(const uint8_t *src, uint8_t *dst) {
	for (int y = 0; y < 200; ++y) {
		int w = 40;
		while (w--) {
			uint8_t p[] = {
				*(src + 8000 * 3),
				*(src + 8000 * 2),
				*(src + 8000 * 1),
				*(src + 8000 * 0)
			};
			for(int j = 0; j < 4; ++j) {
				uint8_t acc = 0;
				for (int i = 0; i < 8; ++i) {
					acc <<= 1;
					acc |= (p[i & 3] & 0x80) ? 1 : 0;
					p[i & 3] <<= 1;
				}
				*dst++ = acc >> 4;
				*dst++ = acc & 0xF;
			}
			++src;
		}
	}	
}

static uint16_t rgb555_to_565(const uint16_t color) {
	const int r = (color >> 10) & 31;
	const int g = (color >>  5) & 31;
	const int b = (color >>  0) & 31;
	return (r << 11) | (g << 6) | b;
}

static void deinterlace555(const uint8_t *src, int w, int h, uint16_t *dst) {
	for (int y = 0; y < h / 2; ++y) {
		for (int x = 0; x < w; ++x) {
			dst[x]     = rgb555_to_565(READ_BE_UINT16(&src[0]));
			dst[w + x] = rgb555_to_565(READ_BE_UINT16(&src[2]));
			src += 4;
		}
		dst += w * 2;
	}
}

void Video::copyBitmapPtr(const uint8_t *src, uint32_t size) {
	if (_res->getDataType() == Resource::DT_DOS || _res->getDataType() == Resource::DT_AMIGA) {
		decode_amiga(src, _tempBitmap);
		_stub->addBitmapToList(0, _tempBitmap, 320, 200, FMT_CLUT);
	} else if (_res->getDataType() == Resource::DT_3DO) {
		deinterlace555(src, 320, 200, _bitmap565);
		_stub->addBitmapToList(0, (uint8_t *)_bitmap565, 320, 200, FMT_RGB565);
	} else {
		int w, h;
		uint8_t *buf = decode_bitmap(src, false, -1, &w, &h);
		if (buf) {
			_stub->addBitmapToList(0, buf, w, h, FMT_RGB);
			free(buf);
		}
	}
}

static void readPaletteWin31(const uint8_t *buf, int num, Color pal[16]) {
	const uint8_t *p = buf + num * 16 * sizeof(uint16_t);
	for (int i = 0; i < 16; ++i) {
		const uint16_t index = READ_LE_UINT16(p); p += 2;
		const uint32_t color = READ_LE_UINT32(buf + 0xC04 + index * sizeof(uint32_t));
		pal[i].r =  color        & 255;
		pal[i].g = (color >>  8) & 255;
		pal[i].b = (color >> 16) & 255;
	}
}

static void readPalette3DO(const uint8_t *buf, int num, Color pal[16]) {
	const uint8_t *p = buf + num * 16 * sizeof(uint16_t);
	for (int i = 0; i < 16; ++i) {
		const uint16_t color = READ_BE_UINT16(p); p += 2;
		const int r = (color >> 10) & 31;
		const int g = (color >>  5) & 31;
		const int b =  color        & 31;
		pal[i].r = (r << 3) | (r >> 2);
		pal[i].g = (g << 3) | (g >> 2);
		pal[i].b = (b << 3) | (b >> 2);
	}
}

static void readPaletteAmiga(const uint8_t *buf, int num, Color pal[16]) {
	const uint8_t *p = buf + num * 16 * sizeof(uint16_t);
	for (int i = 0; i < 16; ++i) {
		const uint16_t color = READ_BE_UINT16(p); p += 2;
		const uint8_t r = (color >> 8) & 0xF;
		const uint8_t g = (color >> 4) & 0xF;
		const uint8_t b =  color       & 0xF;
		pal[i].r = (r << 4) | r;
		pal[i].g = (g << 4) | g;
		pal[i].b = (b << 4) | b;
	}
}

void Video::changePal(uint8_t palNum) {
	if (palNum < 32 && palNum != _currentPal) {
		Color pal[16];
		if (_res->getDataType() == Resource::DT_WIN31) {
			readPaletteWin31(_res->_segVideoPal, palNum, pal);
		} else if (_res->getDataType() == Resource::DT_3DO) {
			readPalette3DO(_res->_segVideoPal, palNum, pal);
		} else {
			readPaletteAmiga(_res->_segVideoPal, palNum, pal);
		}
		_stub->setPalette(pal, 16);
		_currentPal = palNum;
	}
}

void Video::updateDisplay(uint8_t page) {
	debug(DBG_VIDEO, "Video::updateDisplay(%d)", page);
	if (page != 0xFE) {
		if (page == 0xFF) {
			SWAP(_listPtrs[1], _listPtrs[2]);
		} else {
			_listPtrs[1] = getPagePtr(page);
		}
	}
	if (_nextPal != 0xFF) {
		changePal(_nextPal);
		_nextPal = 0xFF;
	}
	_stub->blitList(_listPtrs[1]);
}
