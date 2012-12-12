// Copyright 2012 Google Inc.
//
//   This Source Code Form is subject to the terms of the Mozilla Public
//   License, v. 2.0. If a copy of the MPL was not distributed with this
//   file, You can obtain one at http://mozilla.org/MPL/2.0/.
//

#include "Deserializer.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <iostream>
#include <iomanip>
#include "spug/RCBase.h"
#include "Serializer.h"
#include "DeserializationError.h"

using namespace std;
using namespace model;

unsigned int Deserializer::readUInt(const char *name) {
    uint8_t b = 0x80;
    unsigned val = 0, offset = 0;
    while (b & 0x80) {
        if (src.eof())
            throw DeserializationError("EOF deserializing meta-data");
        b = src.get();

        // see if we've got the last byte
        val = val | ((b & 0x7f) << offset);
        offset += 7;
    }

    if (Serializer::trace)
        cerr << "read uint " << name << ": " << val << endl;
    return val;
}

char *Deserializer::readBlob(size_t &size, char *buffer, const char *name) {
    size_t cap = size;
    size = readUInt(name);

    if (size > cap || !buffer)
        buffer = new char[size];

    src.read(buffer, size);
    if (src.fail())
        throw DeserializationError("EOF deserializing meta-data");
    if (Serializer::trace)
        cerr << "read blob " << name << ": " << setw(size) << buffer << endl;
    return buffer;
}

string Deserializer::readString(size_t expectedMaxSize, const char *name) {
    char buffer[expectedMaxSize];
    size_t size = expectedMaxSize;
    memset(buffer, 0, size);
    char *tmp = readBlob(size, buffer, name);
    if (tmp != buffer) {
        string result(tmp, size);
        delete tmp;
    } else {
        return string(tmp, size);
    }
}

Deserializer::ReadObjectResult Deserializer::readObject(
    const ObjectReader &reader,
    const char *name
) {
    int id = readUInt(name);
    if (id & 1) {
        // this is a definition - let the reader read the object
        if (Serializer::trace)
            cerr << "reading new object " << name << " id = " << id << endl;
        userData = 0;
        spug::RCBasePtr obj = reader.read(*this);
        objMap[id >> 1] = obj;
        return ReadObjectResult(obj, true, userData);
    } else {
        // the object should already exist
        if (Serializer::trace)
            cerr << "reading existing object " << name <<  " id = " << id <<
                endl;
        ObjMap::iterator iter = objMap.find(id >> 1);
        assert(iter != objMap.end() && "Unable to resolve serialized object");
        return ReadObjectResult(iter->second, false, 0);
    }
}
