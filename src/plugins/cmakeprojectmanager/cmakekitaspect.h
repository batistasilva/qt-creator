// Copyright (C) 2016 Canonical Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "cmake_global.h"

#include "cmakeconfigitem.h"

#include <utils/id.h>

namespace ProjectExplorer { class KitAspect; }

namespace CMakeProjectManager {

class CMakeTool;

class CMAKE_EXPORT CMakeKitAspect
{
public:
    static Utils::Id id();

    static Utils::Id cmakeToolId(const ProjectExplorer::Kit *k);
    static CMakeTool *cmakeTool(const ProjectExplorer::Kit *k);
    static void setCMakeTool(ProjectExplorer::Kit *k, const Utils::Id id);
    static QString msgUnsupportedVersion(const QByteArray &versionString);

    static ProjectExplorer::KitAspect *createKitAspect(ProjectExplorer::Kit *k);

private:
    friend class CMakeToolManager;
};

class CMAKE_EXPORT CMakeGeneratorKitAspect
{
public:
    static QString generator(const ProjectExplorer::Kit *k);
    static QString platform(const ProjectExplorer::Kit *k);
    static QString toolset(const ProjectExplorer::Kit *k);
    static void setGenerator(ProjectExplorer::Kit *k, const QString &generator);
    static void setPlatform(ProjectExplorer::Kit *k, const QString &platform);
    static void setToolset(ProjectExplorer::Kit *k, const QString &toolset);
    static void set(ProjectExplorer::Kit *k, const QString &generator,
                    const QString &platform, const QString &toolset);
    static QStringList generatorArguments(const ProjectExplorer::Kit *k);
    static CMakeConfig generatorCMakeConfig(const ProjectExplorer::Kit *k);
    static bool isMultiConfigGenerator(const ProjectExplorer::Kit *k);

    static ProjectExplorer::KitAspect *createKitAspect(ProjectExplorer::Kit *k);
};

class CMAKE_EXPORT CMakeConfigurationKitAspect
{
public:
    static CMakeConfig configuration(const ProjectExplorer::Kit *k);
    static void setConfiguration(ProjectExplorer::Kit *k, const CMakeConfig &config);

    static QString additionalConfiguration(const ProjectExplorer::Kit *k);
    static void setAdditionalConfiguration(ProjectExplorer::Kit *k, const QString &config);

    static QStringList toStringList(const ProjectExplorer::Kit *k);
    static void fromStringList(ProjectExplorer::Kit *k, const QStringList &in);

    static QStringList toArgumentsList(const ProjectExplorer::Kit *k);

    static CMakeConfig defaultConfiguration(const ProjectExplorer::Kit *k);

    static void setCMakePreset(ProjectExplorer::Kit *k, const QString &presetName);
    static CMakeConfigItem cmakePresetConfigItem(const ProjectExplorer::Kit *k);

    static ProjectExplorer::KitAspect *createKitAspect(ProjectExplorer::Kit *k);
};

namespace Internal { void setupCMakeKitAspects(); }

} // CMakeProjectManager
