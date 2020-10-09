//
// Created by Thien Nguyen on 10/8/20.
//

#ifndef PRJ1_SERIALIZER_H
#define PRJ1_SERIALIZER_H

void packi32(unsigned char *buf, unsigned long int i)
{
    *buf++ = i>>24; *buf++ = i>>16;
    *buf++ = i>>8;  *buf++ = i;
}


unsigned long int unpacku32(unsigned char *buf)
{
    return ((unsigned long int)buf[0]<<24) |
           ((unsigned long int)buf[1]<<16) |
           ((unsigned long int)buf[2]<<8)  |
           buf[3];
}
#endif //PRJ1_SERIALIZER_H
