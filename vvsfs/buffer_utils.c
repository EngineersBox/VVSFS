#include "vvsfs.h"

/* Write an unsigned 32-bit integer to a buffer
 *
 * @buf: Byte buffer pointer at the position to
 *       write, aquired from a struct buffer_head
 * @data: Integer to write to buffer
 */
void write_int_to_buffer(char *buf, uint32_t data) {
    buf[0] = (data >> 24) & 0xFF;
    buf[1] = (data >> 16) & 0xFF;
    buf[2] = (data >> 8) & 0xFF;
    buf[3] = data & 0xFF;
}

/* Read an unsigned 32-bit integer from a buffer
 *
 * @buf: Byte buffer pointer at the position to read
 *       from, aquired from a struct buffer_head
 *
 * @return: (uint32_t) read integer value
 */
uint32_t read_int_from_buffer(char *buf) {
    // Because some genius decided to use a char type to represent a byte in
    // the struct buffer_head b_data field (yes thats a signed data type with
    // potentially variable length of either 8 or 16 bits), we have to be very
    // careful with reading signed data from the buffer. Because unsigned data
    // will become signed. Yeah. Fun. I have no idea why they didnt use a
    // uint8_t or arch specific intrinsic 8-bit sized type for a byte. Good
    // thing that struct buffer_head is deprecated in favour of struct bio in
    // linux >2.6 which DOES properly define an unsigned byte type for the
    // internal buffer.
    unsigned char *u_buf = (unsigned char *)buf;
    uint32_t data = 0;
    data |= ((uint32_t)u_buf[0]) << 24;
    data |= ((uint32_t)u_buf[1]) << 16;
    data |= ((uint32_t)u_buf[2]) << 8;
    data |= ((uint32_t)u_buf[3]);
    return data;
}
