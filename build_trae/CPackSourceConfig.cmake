# This file will be configured to contain variables for CPack. These variables
# should be set in the CMake list file of the project before CPack module is
# included. The list of available CPACK_xxx variables and their associated
# documentation may be obtained using
#  cpack --help-variable-list
#
# Some variables are common to all generators (e.g. CPACK_PACKAGE_NAME)
# and some are specific to a generator
# (e.g. CPACK_NSIS_EXTRA_INSTALL_COMMANDS). The generator specific variables
# usually begin with CPACK_<GENNAME>_xxxx.


set(CPACK_BUILD_SOURCE_DIRS "G:/c/2025/lunzi/IrulerDeskpro;G:/c/2025/lunzi/IrulerDeskpro/build_trae")
set(CPACK_CMAKE_GENERATOR "Visual Studio 17 2022")
set(CPACK_COMPONENT_UNSPECIFIED_HIDDEN "TRUE")
set(CPACK_COMPONENT_UNSPECIFIED_REQUIRED "TRUE")
set(CPACK_DEFAULT_PACKAGE_DESCRIPTION_FILE "C:/Program Files/CMake/share/cmake-3.28/Templates/CPack.GenericDescription.txt")
set(CPACK_DEFAULT_PACKAGE_DESCRIPTION_SUMMARY "ScreenStreamApp built using CMake")
set(CPACK_DMG_SLA_USE_RESOURCE_FILE_LICENSE "ON")
set(CPACK_GENERATOR "7Z;ZIP")
set(CPACK_IGNORE_FILES "/CVS/;/\\.svn/;/\\.bzr/;/\\.hg/;/\\.git/;\\.swp\$;\\.#;/#")
set(CPACK_INNOSETUP_ARCHITECTURE "x64")
set(CPACK_INSTALLED_DIRECTORIES "G:/c/2025/lunzi/IrulerDeskpro;/")
set(CPACK_INSTALL_CMAKE_PROJECTS "")
set(CPACK_INSTALL_PREFIX "C:/Program Files (x86)/ScreenStreamApp")
set(CPACK_MODULE_PATH "C:/Qt/6.8.3/msvc2022_64/lib/cmake/Qt6;C:/Qt/6.8.3/msvc2022_64/lib/cmake/Qt6/3rdparty/extra-cmake-modules/find-modules;C:/Qt/6.8.3/msvc2022_64/lib/cmake/Qt6/3rdparty/kwin")
set(CPACK_NSIS_CONTACT "support@iruler.local")
set(CPACK_NSIS_DISPLAY_NAME "IrulerDeskPro")
set(CPACK_NSIS_DISPLAY_NAME_SET "TRUE")
set(CPACK_NSIS_ENABLE_UNINSTALL "ON")
set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL "OFF")
set(CPACK_NSIS_EXECUTABLE "C:/Program Files (x86)/NSIS/makensis.exe")
set(CPACK_NSIS_EXECUTABLES_DIRECTORY ".")
set(CPACK_NSIS_EXTRA_INSTALL_COMMANDS "
    SetShellVarContext all
    CreateShortCut \"$DESKTOP\\IrulerDeskPro.lnk\" \"$INSTDIR\\irulerdesk.exe\"
    CreateShortCut \"$SMSTARTUP\\IrulerDeskPro.lnk\" \"$INSTDIR\\irulerdesk.exe\"
")
set(CPACK_NSIS_EXTRA_PREINSTALL_COMMANDS "ExecWait $"taskkill /F /IM irulerdesk.exe /T$"
ExecWait $"taskkill /F /IM CaptureProcess.exe /T$"
ExecWait $"taskkill /F /IM PlayerProcess.exe /T$"
StrCpy $R0 $"$"
ReadRegStr $R0 HKLM $"Software\Microsoft\Windows\CurrentVersion\Uninstall\IrulerDeskPro$" $"UninstallString$"
StrCmp $R0 $"$" checkHKCU haveUninst
checkHKCU:
ReadRegStr $R0 HKCU $"Software\Microsoft\Windows\CurrentVersion\Uninstall\IrulerDeskPro$" $"UninstallString$"
StrCmp $R0 $"$" doneCheck haveUninst
haveUninst:
MessageBox MB_ICONQUESTION|MB_YESNO $"检测到已安装旧版本，是否先卸载后继续安装？$" IDYES doUninstall IDNO doneCheck
doUninstall:
ExecWait $"$R0$"
doneCheck:
")
set(CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS "
    SetShellVarContext all
    Delete \"$DESKTOP\\IrulerDeskPro.lnk\"
    Delete \"$SMSTARTUP\\IrulerDeskPro.lnk\"
")
set(CPACK_NSIS_HELP_LINK "http://www.iruler.cn/")
set(CPACK_NSIS_INSTALLER_ICON_CODE "")
set(CPACK_NSIS_INSTALLER_MUI_ICON_CODE "")
set(CPACK_NSIS_INSTALL_ROOT "$PROGRAMFILES")
set(CPACK_NSIS_MENU_LINKS "irulerdesk.exe;IrulerDeskPro")
set(CPACK_NSIS_MUI_FINISHPAGE_RUN "irulerdesk.exe")
set(CPACK_NSIS_MUI_ICON "G:/c/2025/lunzi/IrulerDeskpro/src/maps/logo/iruler.ico")
set(CPACK_NSIS_MUI_UNIICON "G:/c/2025/lunzi/IrulerDeskpro/src/maps/logo/iruler.ico")
set(CPACK_NSIS_PACKAGE_NAME "IrulerDeskPro")
set(CPACK_NSIS_UNINSTALL_NAME "Uninstall")
set(CPACK_NSIS_URL_INFO_ABOUT "http://www.iruler.cn/")
set(CPACK_OUTPUT_CONFIG_FILE "G:/c/2025/lunzi/IrulerDeskpro/build_trae/CPackConfig.cmake")
set(CPACK_PACKAGE_DEFAULT_LOCATION "/")
set(CPACK_PACKAGE_DESCRIPTION_FILE "C:/Program Files/CMake/share/cmake-3.28/Templates/CPack.GenericDescription.txt")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "屏幕采集、VP9 传输、Qt 前端一体化客户端")
set(CPACK_PACKAGE_FILE_NAME "IrulerDeskPro-1.0.1-Source")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "IrulerDeskPro")
set(CPACK_PACKAGE_INSTALL_REGISTRY_KEY "IrulerDeskPro")
set(CPACK_PACKAGE_NAME "IrulerDeskPro")
set(CPACK_PACKAGE_RELOCATABLE "true")
set(CPACK_PACKAGE_VENDOR "lunzi")
set(CPACK_PACKAGE_VERSION "1.0.1")
set(CPACK_PACKAGE_VERSION_MAJOR "0")
set(CPACK_PACKAGE_VERSION_MINOR "1")
set(CPACK_PACKAGE_VERSION_PATCH "1")
set(CPACK_RESOURCE_FILE_LICENSE "G:/c/2025/lunzi/IrulerDeskpro/installer/LICENSE_en.txt")
set(CPACK_RESOURCE_FILE_README "C:/Program Files/CMake/share/cmake-3.28/Templates/CPack.GenericDescription.txt")
set(CPACK_RESOURCE_FILE_WELCOME "C:/Program Files/CMake/share/cmake-3.28/Templates/CPack.GenericWelcome.txt")
set(CPACK_RPM_PACKAGE_SOURCES "ON")
set(CPACK_SET_DESTDIR "OFF")
set(CPACK_SOURCE_7Z "ON")
set(CPACK_SOURCE_GENERATOR "7Z;ZIP")
set(CPACK_SOURCE_IGNORE_FILES "/CVS/;/\\.svn/;/\\.bzr/;/\\.hg/;/\\.git/;\\.swp\$;\\.#;/#")
set(CPACK_SOURCE_INSTALLED_DIRECTORIES "G:/c/2025/lunzi/IrulerDeskpro;/")
set(CPACK_SOURCE_OUTPUT_CONFIG_FILE "G:/c/2025/lunzi/IrulerDeskpro/build_trae/CPackSourceConfig.cmake")
set(CPACK_SOURCE_PACKAGE_FILE_NAME "IrulerDeskPro-1.0.1-Source")
set(CPACK_SOURCE_TOPLEVEL_TAG "Windows-Source")
set(CPACK_SOURCE_ZIP "ON")
set(CPACK_STRIP_FILES "")
set(CPACK_SYSTEM_NAME "Windows")
set(CPACK_THREADS "1")
set(CPACK_TOPLEVEL_TAG "Windows-Source")
set(CPACK_WIX_SIZEOF_VOID_P "8")

if(NOT CPACK_PROPERTIES_FILE)
  set(CPACK_PROPERTIES_FILE "G:/c/2025/lunzi/IrulerDeskpro/build_trae/CPackProperties.cmake")
endif()

if(EXISTS ${CPACK_PROPERTIES_FILE})
  include(${CPACK_PROPERTIES_FILE})
endif()
