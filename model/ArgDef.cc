// Copyright 2012 Google Inc.
//
//   This Source Code Form is subject to the terms of the Mozilla Public
//   License, v. 2.0. If a copy of the MPL was not distributed with this
//   file, You can obtain one at http://mozilla.org/MPL/2.0/.
//

#include "ArgDef.h"

#include "builder/Builder.h"
#include "Context.h"
#include "Deserializer.h"
#include "TypeDef.h"

using namespace model;
using namespace std;

ArgDefPtr ArgDef::deserialize(Deserializer &deser) {
    string name = deser.readString(16, "name");
    TypeDefPtr type = TypeDef::deserialize(deser);
    return deser.context->builder.materializeArg(*deser.context, name,
                                                 type.get()
                                                 );
}