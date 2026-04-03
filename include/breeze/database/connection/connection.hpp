//
// Created by venom on 1/25/26.
//

#ifndef BREEZE_CONNECTION_H
#define BREEZE_CONNECTION_H
#pragma once

// This header is an aggregator for granular connection headers. Prefer including
// the specific headers (types.hpp, interfaces.hpp, factory.hpp, *_connection.hpp,
// manager.hpp, utils.hpp) directly. This file remains for backward compatibility.

#include "types.hpp"
#include "interfaces.hpp"
#include "factory.hpp"
#include "sqlite_connection.hpp"
#include "mysql_connection.hpp"
#include "postgresql_connection.hpp"
#include "mongodb_connection.hpp"
#include "manager.hpp"
#include "utils.hpp"

#endif // BREEZE_CONNECTION_H

