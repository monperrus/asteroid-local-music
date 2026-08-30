/*
 * Copyright (C) 2017 - Florent Revest <revestflo@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <asteroidapp.h>
#include <QLockFile>
#include <qqml.h>

#include "localplayback.h"

int main(int argc, char *argv[])
{
    QLockFile instanceLock(QStringLiteral("/tmp/asteroid-local-music.lock"));
    instanceLock.setStaleLockTime(0);
    if (!instanceLock.tryLock(100))
        return 0;

    qmlRegisterType<LocalPlayback>("LocalMusic", 1, 0, "LocalPlayback");
    return AsteroidApp::main(argc, argv);
}
