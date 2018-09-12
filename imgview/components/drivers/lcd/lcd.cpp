/*
This is the subclass graphics library for all our displays, providing a common
set of graphics primitives (points, lines, circles, etc.)

Adafruit invests time and resources providing this open source code, please
support Adafruit & open-source hardware by purchasing products from Adafruit!

Based on ESP8266 library https://github.com/Sermus/ESP8266_Adafruit_lcd

Copyright (c) 2015-2016 Andrey Filimonov.  All rights reserved.

Additions for ESP32 Copyright (c) 2016-2017 Espressif Systems (Shanghai) PTE LTD

Copyright (c) 2013 Adafruit Industries.  All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

- Redistributions of source code must retain the above copyright notice,
  this list of conditions and the following disclaimer.
- Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.
 */


#include "Adafruit_GFX.h"
#include "lcd.h"
#include "st7735s.h"
#include "font7s.h"
#include "glcdfont.h"

#include "esp_partition.h"
#include "esp_log.h"
#include "driver/gpio.h"

#include "freertos/semphr.h"
#include "freertos/task.h"

/*Rotation Defines*/
#define MADCTL_MY  0x80
#define MADCTL_MX  0x40
#define MADCTL_MV  0x20
#define MADCTL_ML  0x10
#define MADCTL_RGB 0x00
#define MADCTL_BGR 0x08
#define MADCTL_MH  0x04


#define SWAPBYTES(i) ((i>>8) | (i<<8))
static const char* TAG = "LCD";

CMyLcd::CMyLcd(lcd_conf_t* lcd_conf, int height, int width, bool dma_en, int dma_word_size, int dma_chan) : Adafruit_GFX(width, height)
{
    m_height = height;
    m_width  = width;
    tabcolor = 0;
    dma_mode = dma_en;
    dma_buf_size = dma_word_size;
    spi_mux = xSemaphoreCreateRecursiveMutex();
    m_dma_chan = dma_chan;
    setSpiBus(lcd_conf);
}

CMyLcd::~CMyLcd()
{
    spi_bus_remove_device(spi_wr);
    vSemaphoreDelete(spi_mux);
}

void CMyLcd::setSpiBus(lcd_conf_t *lcd_conf)
{
    cmd_io = (gpio_num_t) lcd_conf->pin_num_dc;
    dc.dc_io = cmd_io;
    id.id = lcd_init(lcd_conf, &spi_wr, &dc, m_dma_chan);
    id.mfg_id = (id.id >> (8 * 1)) & 0xff ;
    id.lcd_driver_id = (id.id >> (8 * 2)) & 0xff;
    id.lcd_id = (id.id >> (8 * 3)) & 0xff;
}

inline void CMyLcd::setAddrWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    xSemaphoreTakeRecursive(spi_mux, portMAX_DELAY);
    transmitCmdData(LCD_CASET, MAKEWORD(x0 >> 8, x0 & 0xFF, x1 >> 8, x1 & 0xFF));
    transmitCmdData(LCD_PASET, MAKEWORD(y0 >> 8, y0 & 0xFF, y1 >> 8, y1 & 0xFF));
    transmitCmd(LCD_RAMWR); // write to RAM
    xSemaphoreGiveRecursive(spi_mux);
}

inline void CMyLcd::transmitData(uint16_t data)
{
    xSemaphoreTakeRecursive(spi_mux, portMAX_DELAY);
    lcd_data(spi_wr, (uint8_t *)&data, 2, &dc);
    xSemaphoreGiveRecursive(spi_mux);
}
inline void CMyLcd::transmitCmdData(uint8_t cmd, uint32_t data)
{
    xSemaphoreTakeRecursive(spi_mux, portMAX_DELAY);
    lcd_cmd(spi_wr, cmd, &dc);
    lcd_data(spi_wr, (uint8_t *)&data, 4, &dc);
    xSemaphoreGiveRecursive(spi_mux);
}
inline void CMyLcd::transmitData(uint16_t data, int32_t repeats)
{
    xSemaphoreTakeRecursive(spi_mux, portMAX_DELAY);
    lcd_send_uint16_r(spi_wr, data, repeats, &dc);
    xSemaphoreGiveRecursive(spi_mux);
}
inline void CMyLcd::transmitData(uint8_t* data, int length)
{
    xSemaphoreTakeRecursive(spi_mux, portMAX_DELAY);
    lcd_data(spi_wr, (uint8_t *)data, length, &dc);
    xSemaphoreGiveRecursive(spi_mux);
}
inline void CMyLcd::transmitCmd(uint8_t cmd)
{
    xSemaphoreTakeRecursive(spi_mux, portMAX_DELAY);
    lcd_cmd(spi_wr, cmd, &dc);
    xSemaphoreGiveRecursive(spi_mux);
}

void CMyLcd::transmitCmdData(uint8_t cmd, const uint8_t data, uint8_t numDataByte)
{
    xSemaphoreTakeRecursive(spi_mux, portMAX_DELAY);
    lcd_cmd(spi_wr, (const uint8_t) cmd, &dc);
    lcd_data(spi_wr, &data, 1, &dc);
    xSemaphoreGiveRecursive(spi_mux);
}

uint32_t CMyLcd::getLcdId()
{
    xSemaphoreTakeRecursive(spi_mux, portMAX_DELAY);
    uint32_t id = lcd_get_id(spi_wr, &dc);
    xSemaphoreGiveRecursive(spi_mux);
    return id;
}

void CMyLcd::drawPixel(int16_t x, int16_t y, uint16_t color)
{
    if ((x < 0) || (x >= _width) || (y < 0) || (y >= _height)) {
        return;
    }
    xSemaphoreTakeRecursive(spi_mux, portMAX_DELAY);
    setAddrWindow(x, y, x + 1, y + 1);
    transmitData(SWAPBYTES(color));
    xSemaphoreGiveRecursive(spi_mux);
}

void CMyLcd::_fastSendBuf(const uint16_t* buf, int point_num, bool swap)
{
    if ((point_num * sizeof(uint16_t)) <= (16 * sizeof(uint32_t))) {
        transmitData((uint8_t*) buf, sizeof(uint16_t) * point_num);
    } else {
        int gap_point = dma_buf_size;
        uint16_t* data_buf = (uint16_t*) malloc(gap_point * sizeof(uint16_t));
        int offset = 0;
        while (point_num > 0) {
            int trans_points = point_num > gap_point ? gap_point : point_num;

            if (swap) {
                for (int i = 0; i < trans_points; i++) {
                    data_buf[i] = SWAPBYTES(buf[i + offset]);
                }
            } else {
                memcpy((uint8_t*) data_buf, (uint8_t*) (buf + offset), trans_points * sizeof(uint16_t));
            }
            transmitData((uint8_t*) (data_buf), trans_points * sizeof(uint16_t));
            offset += trans_points;
            point_num -= trans_points;
        }
        free(data_buf);
        data_buf = NULL;
    }
}

void CMyLcd::_fastSendRep(uint16_t val, int rep_num)
{
    int point_num = rep_num;
    int gap_point = dma_buf_size;
    gap_point = (gap_point > point_num ? point_num : gap_point);

    uint16_t* data_buf = (uint16_t*) malloc(gap_point * sizeof(uint16_t));
    int offset = 0;
    while (point_num > 0) {
        for (int i = 0; i < gap_point; i++) {
            data_buf[i] = val;
        }
        int trans_points = point_num > gap_point ? gap_point : point_num;
        transmitData((uint8_t*) (data_buf), sizeof(uint16_t) * trans_points);
        offset += trans_points;
        point_num -= trans_points;
    }
    free(data_buf);
    data_buf = NULL;
}

void CMyLcd::drawBitmap(int16_t x, int16_t y, const uint16_t *bitmap, int16_t w, int16_t h)
{
    xSemaphoreTakeRecursive(spi_mux, portMAX_DELAY);
    setAddrWindow(x, y, x + w - 1, y + h - 1);
    if (dma_mode) {
        _fastSendBuf(bitmap, w * h);
    } else {
        for (int i = 0; i < w * h; i++) {
            transmitData(SWAPBYTES(bitmap[i]), 1);
        }
    }
    xSemaphoreGiveRecursive(spi_mux);
}

esp_err_t CMyLcd::drawBitmapFromFlashPartition(int16_t x, int16_t y, int16_t w, int16_t h, esp_partition_t* data_partition, int data_offset, int malloc_pixal_size, bool swap_bytes_en)
{
    if (data_partition == NULL) {
        ESP_LOGE(TAG, "Partition error, null!");
        return ESP_FAIL;
    }
    xSemaphoreTakeRecursive(spi_mux, portMAX_DELAY);
    uint16_t* recv_buf = (uint16_t*) calloc(malloc_pixal_size, sizeof(uint16_t));
    setAddrWindow(x, y, x + w - 1, y + h - 1);

    int offset = 0;
    int point_num = w * h;
    while (point_num) {
        int len = malloc_pixal_size > point_num ? point_num : malloc_pixal_size;
        esp_partition_read(data_partition, data_offset + offset * sizeof(uint16_t), (uint8_t*) recv_buf, len * sizeof(uint16_t));
        if (swap_bytes_en) {
            for (int i = 0; i < len; i++) {
                recv_buf[i] = SWAPBYTES(recv_buf[i]);
            }
        }
        transmitData((uint8_t*) recv_buf, len * sizeof(uint16_t));
        offset += len;
        point_num -= len;
    }
    free(recv_buf);
    recv_buf = NULL;
    xSemaphoreGiveRecursive(spi_mux);
    return ESP_OK;
}

void CMyLcd::drawBitmapFont(int16_t x, int16_t y, uint8_t w, uint8_t h, const uint16_t *bitmap)
{
    //Saves some memory and SWAPBYTES as compared to above API
    xSemaphoreTakeRecursive(spi_mux, portMAX_DELAY);
    setAddrWindow(x, y, x + w - 1, y + h - 1);
    if (dma_mode) {
        _fastSendBuf(bitmap, w * h, false);
    } else {
        transmitData((uint8_t*) bitmap, sizeof(uint16_t) * w * h);
    }
    xSemaphoreGiveRecursive(spi_mux);
}

void CMyLcd::drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color)
{
    // Rudimentary clipping
    if ((x >= _width) || (y >= _height)) {
        return;
    }
    if ((y + h - 1) >= _height) {
        h = _height - y;
    }
    xSemaphoreTakeRecursive(spi_mux, portMAX_DELAY);
    setAddrWindow(x, y, x, y + h - 1);
    if (dma_mode) {
        _fastSendRep(SWAPBYTES(color), h);
    } else {
        transmitData(SWAPBYTES(color), h);
    }
    xSemaphoreGiveRecursive(spi_mux);
}

void CMyLcd::drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color)
{
    // Rudimentary clipping
    if ((x >= _width) || (y >= _height)) {
        return;
    }
    if ((x + w - 1) >= _width) {
        w = _width - x;
    }
    xSemaphoreTakeRecursive(spi_mux, portMAX_DELAY);
    setAddrWindow(x, y, x + w - 1, y);
    if (dma_mode) {
        _fastSendRep(SWAPBYTES(color), w);
    } else {
        transmitData(SWAPBYTES(color), w);
    }
    xSemaphoreGiveRecursive(spi_mux);
}

void CMyLcd::fillScreen(uint16_t color)
{
    fillRect(0, 0, _width, _height, color);
}

void CMyLcd::fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color)
{
    // rudimentary clipping (drawChar w/big text requires this)
    if ((x >= _width) || (y >= _height)) {
        return;
    }
    if ((x + w - 1) >= _width) {
        w = _width - x;
    }
    if ((y + h - 1) >= _height) {
        h = _height - y;
    }
    xSemaphoreTakeRecursive(spi_mux, portMAX_DELAY);
    setAddrWindow(x, y, x + w - 1, y + h - 1);
    if (dma_mode) {
        _fastSendRep(SWAPBYTES(color), h * w);
    } else {
        transmitData(SWAPBYTES(color), h * w);
    }
    xSemaphoreGiveRecursive(spi_mux);
}

uint16_t CMyLcd::color565(uint8_t r, uint8_t g, uint8_t b)
{
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

void CMyLcd::scrollTo(uint16_t y)
{
    xSemaphoreTakeRecursive(spi_mux, portMAX_DELAY);
    transmitCmd(0x37);
    transmitData(y);
    xSemaphoreGiveRecursive(spi_mux);
}

void CMyLcd::setRotation(uint8_t m)
{
    uint8_t data = 0;
    rotation = m % 7;  //Can't be more than 6
    switch (rotation) {
    case 0:
    	data = 0 | MADCTL_RGB; // Normal
    	_width = m_width;
    	_height = m_height;
    	break;
    case 1:
		data = MADCTL_MV | MADCTL_MY | MADCTL_RGB; // X-Y Exchange && Y-Mirror
		_width = m_height;
		_height = m_width;
		break;
    case 2:
    	data = MADCTL_MX | MADCTL_MY | MADCTL_RGB; // X-Mirror && Y-Mirror
    	_width = m_width;
    	_height = m_height;
    	break;
    case 3:
    	data = MADCTL_MV | MADCTL_MX | MADCTL_RGB; // X-Y Exchange && X-Mirror
    	_width = m_height;
    	_height = m_width;
    	break;
    case 4:
    	data = MADCTL_MX | MADCTL_RGB; // X-Mirror
		_width = m_width;
		_height = m_height;
		break;
    case 5:
    	data = MADCTL_MY | MADCTL_RGB; // Y-Mirror
    	_width = m_width;
    	_height = m_height;
    	break;
    case 6:
    	data = MADCTL_MV | MADCTL_RGB; // X-Y Exchange
    	_width = m_width;
    	_height = m_height;
    	break;
    }
    transmitCmdData(LCD_MADCTL, data, 1);
}

void CMyLcd::invertDisplay(bool i)
{
    transmitCmd(i ? LCD_INVON : LCD_INVOFF);
}


int CMyLcd::drawFloatSevSeg(float floatNumber, uint8_t decimal, uint16_t poX, uint16_t poY, uint8_t size)
{
    unsigned int temp = 0;
    float decy = 0.0;
    float rounding = 0.5;
    float eep = 0.000001;
    int sumX = 0;
    uint16_t xPlus = 0;

    if (floatNumber - 0.0 < eep) {
        xPlus = drawUnicodeSevSeg('-', poX, poY, size);
        floatNumber = -floatNumber;
        poX  += xPlus;
        sumX += xPlus;
    }

    for (unsigned char i = 0; i < decimal; ++i) {
        rounding /= 10.0;
    }
    floatNumber += rounding;
    temp = (long)floatNumber;
    xPlus = drawNumberSevSeg(temp, poX, poY, size);
    poX  += xPlus;
    sumX += xPlus;

    if (decimal > 0) {
        xPlus = drawUnicodeSevSeg('.', poX, poY, size);
        poX += xPlus;                                       /* Move cursor right   */
        sumX += xPlus;
    } else {
        return sumX;
    }

    decy = floatNumber - temp;
    for (unsigned char i = 0; i < decimal; i++) {
        decy *= 10;                                         /* For the next decimal*/
        temp = decy;                                        /* Get the decimal     */
        xPlus = drawNumberSevSeg(temp, poX, poY, size);
        poX += xPlus;                                       /* Move cursor right   */
        sumX += xPlus;
        decy -= temp;
    }
    return sumX;
}

int CMyLcd::drawUnicodeSevSeg(uint16_t uniCode, uint16_t x, uint16_t y, uint8_t size)
{
    if (size) {
        uniCode -= 32;
    }
    uint16_t width = 0;
    uint16_t height = 0;
    const uint8_t *flash_address = 0;
    int8_t gap = 0;

    if (size == 7) {
        flash_address = chrtbl_f7s[uniCode];
        width = *(widtbl_f7s + uniCode);
        height = CHR_HGT_F7S;
        gap = 2;
    }

    uint16_t w = (width + 7) / 8;
    uint8_t line = 0;

    xSemaphoreTakeRecursive(spi_mux, portMAX_DELAY);
    setAddrWindow(x, y, x + w * 8 - 1, y + height - 1);
    uint16_t* data_buf = (uint16_t*) malloc(dma_buf_size * sizeof(uint16_t));
    int point_num = w * height * 8;
    int idx = 0;
    int trans_points = point_num > dma_buf_size ? dma_buf_size : point_num;
    for (int i = 0; i < height; i++) {
        for (int j = 0; j < w; j++) {
            line = *(flash_address + w * i + j);
            for (int m = 0; m < 8; m++) {
                if ((line >> (7 - m)) & 0x1) {
                    data_buf[idx++] = SWAPBYTES(textcolor);
                } else {
                    data_buf[idx++] = SWAPBYTES(textbgcolor);
                }

                if (idx >= trans_points) {
                    transmitData((uint8_t*) (data_buf), trans_points * sizeof(uint16_t));
                    point_num -= trans_points;
                    idx = 0;
                    trans_points = point_num > dma_buf_size ? dma_buf_size : point_num;
                }

            }
        }
    }
    free(data_buf);
    data_buf = NULL;
    xSemaphoreGiveRecursive(spi_mux);
    return width + gap;
}

int CMyLcd::drawStringSevSeg(const char *string, uint16_t poX, uint16_t poY, uint8_t size)
{
    uint16_t sumX = 0;
    while (*string) {
        uint16_t xPlus = drawUnicodeSevSeg(*string, poX, poY, size);
        sumX += xPlus;
        string++;
        poX += xPlus;                                     /* Move cursor right*/
    }
    return sumX;
}

int CMyLcd::drawNumberSevSeg(int long_num, uint16_t poX, uint16_t poY, uint8_t size)
{
    char tmp[10];
    if (long_num < 0) {
        snprintf(tmp, sizeof(tmp), "%d", long_num);
    } else {
        snprintf(tmp, sizeof(tmp), "%u", long_num);
    }
    return drawStringSevSeg(tmp, poX, poY, size);
}

int CMyLcd::write_char(uint8_t c)
{
    if (!gfxFont) { // 'Classic' built-in font
        if (c == '\n') {                       // Newline?
            cursor_x  = 0;                     // Reset x to zero,
            cursor_y += textsize * 8;          // advance y one line
        }
        else if (c != '\r') {                  // Ignore carriage returns
            if (wrap && ((cursor_x + textsize * 6) > _width)) { // Off right?
                cursor_x  = 0;                 // Reset x to zero,
                cursor_y += textsize * 8;      // advance y one line
            }
            drawChar(cursor_x, cursor_y, c, textcolor, textbgcolor, textsize);
            cursor_x += textsize * 6;          // Advance x one char
        }
    }

    else {
        if (c == '\n') {
            cursor_x  = 0;
            cursor_y += (int16_t)textsize * (uint8_t)gfxFont->yAdvance;
        }
        else if (c != '\r') {
            uint8_t first = gfxFont->first;
            if ((c >= first) && (c <= (uint8_t)gfxFont->last)) {

                GFXglyph *glyph = &(((GFXglyph *)gfxFont->glyph))[c - first];
                uint8_t   w     = glyph->width,
                          h     = glyph->height;

                if ((w > 0) && (h > 0)) {                // Is there an associated bitmap?
                    int16_t xo = (int8_t)glyph->xOffset; // sic
                    if (wrap && ((cursor_x + textsize * (xo + w)) > _width)) {
                        cursor_x  = 0;
                        cursor_y += (int16_t)textsize * (uint8_t)gfxFont->yAdvance;
                    }
                    drawChar(cursor_x, cursor_y, c, textcolor, textbgcolor, textsize);
                }
                cursor_x += (uint8_t)glyph->xAdvance * (int16_t)textsize;
            }
        }
    }
    return cursor_x;
}

int CMyLcd::drawString(const char *string, uint16_t x, uint16_t y)
{
    uint16_t xPlus = x;
    setCursor(xPlus, y);
    while (*string) {
        xPlus = write_char(*string);        // write_char string char-by-char                 
        setCursor(xPlus, y);       // increment cursor
        string++;                      // Move cursor right
    }
    return xPlus;
}

int CMyLcd::drawNumber(int long_num, uint16_t poX, uint16_t poY)
{
    char tmp[10];
    if (long_num < 0) {
        snprintf(tmp, sizeof(tmp), "%d", long_num);
    } else {
        snprintf(tmp, sizeof(tmp), "%u", long_num);
    }
    return drawString(tmp, poX, poY);
}

int CMyLcd::drawFloat(float floatNumber, uint8_t decimal, uint16_t poX, uint16_t poY)
{
    unsigned int temp = 0;
    float decy = 0.0;
    float rounding = 0.5;
    float eep = 0.000001;
    uint16_t xPlus = 0;

    if (floatNumber - 0.0 < eep) {
        xPlus       = drawString("-", poX, poY);
        floatNumber = -floatNumber;
        poX         = xPlus;
    }

    for (unsigned char i = 0; i < decimal; ++i) {
        rounding /= 10.0;
    }
    
    floatNumber += rounding;
    temp         = (long)floatNumber;
    xPlus        = drawNumber(temp, poX, poY);
    poX          = xPlus;

    if (decimal > 0) {
        xPlus = drawString(".", poX, poY);
        poX   = xPlus;                                       /* Move cursor right   */
    } else {
        return poX;
    }

    decy = floatNumber - temp;
    for (unsigned char i = 0; i < decimal; i++) {
        decy *= 10;                                         /* For the next decimal*/
        temp  = decy;                                        /* Get the decimal     */
        xPlus = drawNumber(temp, poX, poY);
        poX   = xPlus;                                       /* Move cursor right   */
        decy -= temp;
    }
    return poX;
}

void CMyLcd::drawChar(int16_t x, int16_t y, unsigned char c, uint16_t color, uint16_t bg, uint8_t size)
{
    uint16_t swapped_foregnd_color = SWAPBYTES(color);  //SWAP earlier, or use SPI LSB first mode
    uint16_t swapped_backgnd_color = SWAPBYTES(bg);

    if(!gfxFont) { // 'Classic' built-in font

        if((x >= _width)            || // Clip right
           (y >= _height)           || // Clip bottom
           ((x + 6 * size - 1) < 0) || // Clip left
           ((y + 8 * size - 1) < 0))   // Clip top
            return;

        if(!_cp437 && (c >= 176)) c++; // Handle 'classic' charset behavior

        //Check if bg!=color which is a flag for transperant backgrounds in our case
        if(color != bg) {
            //Make a premade bmp canvas and push the entire char canvas to the screen
            uint16_t bmp[8][5];
            for (uint8_t i = 0; i < 5; i++) { // Char bitmap = 5 columns
                uint8_t line = font[c * 5 + i];
                for (uint8_t j = 0; j < 8; j++, line >>= 1) {
                    if(line & 1) {
                        if(size == 1) {
                            bmp[j][i] = swapped_foregnd_color;
                            //drawPixel(x+i, y+j, color); //The Old Adafruit way
                        }
                        else {
                            fillRect(x+i*size, y+j*size, size, size, color); //not as fast... set font size = 1 for speed
                        }
                    }
                    else {
                        bmp[j][i] = swapped_backgnd_color;
                    }
                }
            }

            if(size == 1) {
                uint16_t bmp_arr[40];
                for (uint8_t cnt = 0; cnt < 40; cnt++) {
                    bmp_arr[cnt] = (bmp[(uint8_t)(cnt / 5)][(uint8_t)(cnt % 5)]); //5*8 Matrix to 40 element Array
                }
                drawBitmapFont(x, y, 5, 8, bmp_arr); //Speeds up fonts instead of using drawPixel
            }
        }

        //If user wants transperant background, check for transperancy flag which is (color == bg)
        else if(color == bg) {
            //Can't speedup fonts since no pushing pre-made canvas not possible, the fonts printed by this loop will be slow
            for (uint8_t i = 0; i < 5; i++) { // Char bitmap = 5 columns
                uint8_t line = font[c * 5 + i];
                for (uint8_t j = 0; j < 8; j++, line >>= 1) {
                    if(line & 1) {
                        if(size == 1) {
                            drawPixel(x+i, y+j, color); //The Old Adafruit way
                        }
                        else {
                            fillRect(x+i*size, y+j*size, size, size, color); //not as fast... set font size = 1 for speed
                        }
                    }
                }
            }
        }
    }
    else {
        // Custom font
        // Character is assumed previously filtered by write_char() to eliminate
        // newlines, returns, non-printable characters, etc.  Calling
        // drawChar() directly with 'bad' characters of font may cause mayhem!

        c -= (uint8_t)gfxFont->first;
        GFXglyph *glyph  = &(((GFXglyph *)(gfxFont->glyph))[c]);
        uint8_t  *bitmap = (uint8_t *)(gfxFont->bitmap);

        uint16_t bo = glyph->bitmapOffset;
        uint8_t  w  = glyph->width,
                 h  = glyph->height;
        int8_t   xo = glyph->xOffset,
                 yo = glyph->yOffset;
        uint8_t  xx, yy, bits = 0, bit = 0;
        int16_t  xo16 = 0, yo16 = 0;

        if(size > 1) {
            xo16 = xo;
            yo16 = yo;
        }
        if(color != bg) {
            uint16_t bmp[w+5][h+5]; //w*h plus some extra space
            for(yy=0; yy<h; yy++) {
                for(xx=0; xx<w; xx++) {
                    if(!(bit++ & 7)) {
                        bits = bitmap[bo++];
                    }

                    if(bits & 0x80) {
                        if(size == 1) {
                            //drawPixel(x+xo+xx, y+yo+yy, color); //Old adafruit way
                            bmp[yy][xx] = swapped_foregnd_color;
                        }
                        else {
                            fillRect(x+(xo16+xx)*size, y+(yo16+yy)*size, size, size, color);
                        }
                    }

                    if(!(bits & 0x80)) {
                        bmp[yy][xx] = swapped_backgnd_color;
                    }
                    bits <<= 1;
                }
            }

            if (size == 1) {
                uint16_t bmp_arr[w*h];
                for (uint8_t cnt = 0; cnt < w*h; cnt++) {
                    bmp_arr[cnt] = (bmp[(uint8_t)(cnt / w)][(uint8_t)(cnt % w)]);
                }
                drawBitmapFont(x+xo, y+yo, w, h, bmp_arr); //Speeds up fonts instead of using drawPixel
            }
        }

        else if(color == bg) {
            //Unimplemented: Transparent bg for custom fonts
            ESP_LOGE("LCD", "Custom fonts with transparent bg not supported yet\n");
        }
    } // End classic vs custom font
}
