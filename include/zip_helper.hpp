#pragma once

#include <string>
#include <functional>
#include <vector>

namespace RomM3DS {

class ZipHelper {
public:
    using LogCallback = std::function<void(const std::string&)>;

    /**
     * @brief Determines whether a given file is a valid ZIP archive
     * by checking extension or the 'PK\x03\x04' magic header.
     */
    static bool isZipFile(const std::string& filePath);

    /**
     * @brief Extracts all entries from a zip archive into the destination directory.
     * @param zipFilePath Path to the .zip archive.
     * @param destDirectory Directory where contents should be placed.
     * @param log Callback for progress/error messages.
     * @param autoUnnest If true, un-nests a single root subdirectory so contents
     *                   are directly at destDirectory.
     * @return true on success, false on failure.
     */
    static bool extractZip(const std::string& zipFilePath, const std::string& destDirectory,
                           LogCallback log = nullptr, bool autoUnnest = true);

    /**
     * @brief Creates a new zip archive containing all files and folders in sourceDirectory.
     * @param zipFilePath Destination path for the new .zip file.
     * @param sourceDirectory Directory whose contents will be archived.
     * @param log Callback for progress/error messages.
     * @return true on success, false on failure.
     */
    static bool createZipFromDirectory(const std::string& zipFilePath, const std::string& sourceDirectory,
                                       LogCallback log = nullptr);

    /**
     * @brief Counts regular files and directories in a directory (non-recursive).
     */
    static void getDirectoryStats(const std::string& dirPath, int& outFiles, int& outDirs);

    /**
     * @brief If destDir contains only a single directory and no files at the root,
     * moves all files from that directory up to destDir.
     */
    static void unnestDirectoryIfSingle(const std::string& destDir, LogCallback log = nullptr);
};

} // namespace RomM3DS
