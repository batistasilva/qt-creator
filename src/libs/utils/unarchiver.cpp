// Copyright (C) 2020 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "unarchiver.h"

#include "algorithm.h"
#include "hostosinfo.h"
#include "mimeutils.h"
#include "qtcassert.h"
#include "utilstr.h"

#include <QSettings>

using namespace Tasking;

namespace Utils {

namespace {

struct Tool
{
    CommandLine command;
    QStringList supportedMimeTypes;
    FilePaths additionalSearchDirs;
};

} // anon

static FilePaths additionalInstallDirs(const QString &registryKey, const QString &valueName)
{
#if defined(Q_OS_WIN)
    const QSettings settings64(registryKey, QSettings::Registry64Format);
    const QSettings settings32(registryKey, QSettings::Registry32Format);
    return {FilePath::fromVariant(settings64.value(valueName)),
            FilePath::fromVariant(settings32.value(valueName))};
#else
    Q_UNUSED(registryKey)
    Q_UNUSED(valueName)
    return {};
#endif
}

static const QList<Tool> &sTools()
{
    static QList<Tool> tools;
    if (tools.isEmpty()) {
        // clang-format off
        if (HostOsInfo::isWindowsHost()) {
            tools << Tool{{"powershell", "-command Expand-Archive -Force '%{src}' '%{dest}'", CommandLine::Raw},
                          {"application/zip"},
                          {}};
        }
        tools << Tool{{"unzip", {"-o", "%{src}", "-d", "%{dest}"}}, {"application/zip"}, {}};
        tools << Tool{{"7z", {"x", "-o%{dest}", "-y", "-bb", "%{src}"}},
                      {"application/zip", "application/x-7z-compressed"},
                      additionalInstallDirs("HKEY_CURRENT_USER\\Software\\7-Zip", "Path")};
        const FilePaths additionalCMakeDirs =
                additionalInstallDirs("HKEY_LOCAL_MACHINE\\SOFTWARE\\Kitware\\CMake",
                                      "InstallDir");
        // CMake does not rely on tar, but actually uses libarchive directly, so this
        // is more reliable than tar itself
        tools << Tool{{"cmake", {"-E", "tar", "xvf", "%{src}"}},
                      {"application/zip", "application/x-tar", "application/x-7z-compressed"},
                      additionalCMakeDirs};
        tools << Tool{{"cmake", {"-E", "tar", "xvzf", "%{src}"}},
                      {"application/x-compressed-tar"},
                      additionalCMakeDirs};
        tools << Tool{{"cmake", {"-E", "tar", "xvJf", "%{src}"}},
                      {"application/x-xz-compressed-tar"},
                      additionalCMakeDirs};
        tools << Tool{{"cmake", {"-E", "tar", "xvjf", "%{src}"}},
                      {"application/x-bzip-compressed-tar"},
                      additionalCMakeDirs};
        // Put tar near the end. It might not work for 7z, and also the other methods
        // depend on other tools being installed.
        tools << Tool{{"tar", {"xvf", "%{src}"}},
                      {"application/zip", "application/x-tar", "application/x-7z-compressed"},
                      {}};
        tools << Tool{{"tar", {"xvzf", "%{src}"}}, {"application/x-compressed-tar"}, {}};
        tools << Tool{{"tar", {"xvJf", "%{src}"}}, {"application/x-xz-compressed-tar"}, {}};
        tools << Tool{{"tar", {"xvjf", "%{src}"}}, {"application/x-bzip-compressed-tar"}, {}};
        // Keep this at the end so its only used as last resort. Otherwise it might be used for
        // .tar.gz files.
        tools << Tool{{"gzip", {"-d", "%{src}", "-c"}}, {"application/gzip"}, {}};
        // clang-format on
    }
    return tools;
}

static QList<Tool> toolsForMimeType(const MimeType &mimeType)
{
    return Utils::filtered(sTools(), [mimeType](const Tool &tool) {
        return Utils::anyOf(tool.supportedMimeTypes,
                            [mimeType](const QString &mt) { return mimeType.inherits(mt); });
    });
}

static QList<Tool> toolsForFilePath(const FilePath &fp)
{
    return toolsForMimeType(mimeTypeForFile(fp));
}

static std::optional<Tool> resolveTool(const Tool &tool)
{
    const FilePath executable =
        tool.command.executable().withExecutableSuffix().searchInPath(tool.additionalSearchDirs);
    Tool resolvedTool = tool;
    resolvedTool.command.setExecutable(executable);
    return executable.isEmpty() ? std::nullopt : std::make_optional(resolvedTool);
}

expected_str<Unarchiver::SourceAndCommand> Unarchiver::sourceAndCommand(const FilePath &sourceFile)
{
    const QList<Tool> tools = toolsForFilePath(sourceFile);
    if (tools.isEmpty())
        return make_unexpected(Tr::tr("File format not supported."));

    for (const Tool &tool : tools) {
        const std::optional<Tool> resolvedTool = resolveTool(tool);
        if (resolvedTool)
            return SourceAndCommand(sourceFile, resolvedTool->command);
    }

    const QStringList execs = transform<QStringList>(tools, [](const Tool &tool) {
        return tool.command.executable().toUserOutput();
    });
    return make_unexpected(Tr::tr("Could not find any unarchiving executable in PATH (%1).")
                               .arg(execs.join(", ")));
}

static CommandLine unarchiveCommand(const CommandLine &commandTemplate, const FilePath &sourceFile,
                                    const FilePath &destDir)
{
    const QStringList args = Utils::transform(commandTemplate.splitArguments(), [&](auto arg) {
        return arg.replace("%{src}", sourceFile.path()).replace("%{dest}", destDir.path());
    });
    return CommandLine(commandTemplate.executable(), args);
}

void Unarchiver::start()
{
    QTC_ASSERT(!m_process, emit done(DoneResult::Error); return);

    if (!m_sourceAndCommand) {
        emit outputReceived(Tr::tr("No source file set."));
        emit done(DoneResult::Error);
        return;
    }
    if (m_destDir.isEmpty()) {
        emit outputReceived(Tr::tr("No destination directory set."));
        emit done(DoneResult::Error);
        return;
    }

    const CommandLine command = unarchiveCommand(m_sourceAndCommand->m_commandTemplate,
                                                 m_sourceAndCommand->m_sourceFile, m_destDir);
    m_destDir.ensureWritableDir();

    if (command.executable().fileName() == "gzip") {
        std::shared_ptr<QFile> outputFile = std::make_shared<QFile>(
            (m_destDir / m_gzipFileDestName).toFSPathString());

        if (!outputFile->open(QIODevice::WriteOnly)) {
            emit outputReceived(Tr::tr("Failed to open output file."));
            emit done(DoneResult::Error);
            return;
        }

        m_process.reset(new Process);
        QObject::connect(m_process.get(), &Process::readyReadStandardOutput, this, [this, outputFile] {
            const QByteArray data = m_process->readAllRawStandardOutput();
            if (outputFile->write(data) != data.size()) {
                emit outputReceived(Tr::tr("Failed to write output file."));
                emit done(DoneResult::Error);
            }
        });
        QObject::connect(m_process.get(), &Process::readyReadStandardError, this, [this] {
            emit outputReceived(m_process->readAllStandardError());
        });
        QObject::connect(m_process.get(), &Process::done, this, [outputFile, this] {
            outputFile->close();
            const bool success = m_process->result() == ProcessResult::FinishedWithSuccess;
            if (!success) {
                outputFile->remove();
                emit outputReceived(Tr::tr("Command failed."));
            }
            emit done(toDoneResult(success));
        });
        emit outputReceived(
            Tr::tr("Running %1\nin \"%2\".\n\n", "Running <cmd> in <workingdirectory>")
                .arg(command.toUserOutput(), m_destDir.toUserOutput()));
        m_process->setCommand(command);
        m_process->setWorkingDirectory(m_destDir);
        m_process->start();
        return;
    }

    m_process.reset(new Process);
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    QObject::connect(m_process.get(), &Process::readyReadStandardOutput, this, [this] {
        emit outputReceived(m_process->readAllStandardOutput());
    });
    QObject::connect(m_process.get(), &Process::done, this, [this] {
        const bool success = m_process->result() == ProcessResult::FinishedWithSuccess;
        if (!success)
            emit outputReceived(Tr::tr("Command failed."));
        emit done(toDoneResult(success));
    });

    emit outputReceived(Tr::tr("Running %1\nin \"%2\".\n\n", "Running <cmd> in <workingdirectory>")
                            .arg(command.toUserOutput(), m_destDir.toUserOutput()));

    m_process->setCommand(command);
    m_process->setWorkingDirectory(m_destDir);
    m_process->start();
}

UnarchiverTaskAdapter::UnarchiverTaskAdapter()
{
    connect(task(), &Unarchiver::done, this, &TaskInterface::done);
}

void UnarchiverTaskAdapter::start()
{
    task()->start();
}

} // namespace Utils
