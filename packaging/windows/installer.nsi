; PxModRim NSIS Installer Script
!include "MUI2.nsh"
!include "FileFunc.nsh"

!define PRODUCT_NAME "PxModRim"
!define PRODUCT_PUBLISHER "PyXiion"
!define PRODUCT_WEB_SITE "https://github.com/PyXiion/PxModRim"
!define PRODUCT_DIR_REGKEY "Software\Microsoft\Windows\CurrentVersion\App Paths\PxModRim.exe"
!define PRODUCT_UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_NAME}"

!ifndef VERSION
  !define VERSION "0.1.0"
!endif

!ifndef DIST_DIR
  !define DIST_DIR "..\..\dist\entrypoint.dist"
!endif

!ifndef OUTPUT_FILE
  !define OUTPUT_FILE "..\..\dist\PxModRim-Setup.exe"
!endif

Name "${PRODUCT_NAME} ${VERSION}"
OutFile "${OUTPUT_FILE}"
InstallDir "$PROGRAMFILES64\PxModRim"
InstallDirRegKey HKLM "${PRODUCT_DIR_REGKEY}" ""
ShowInstDetails show
ShowUnInstDetails show
RequestExecutionLevel admin

; UI settings
!define MUI_ABORTWARNING
!define MUI_ICON "..\logo.ico"
!define MUI_UNICON "..\logo.ico"

; Installer pages
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_RUN "$INSTDIR\PxModRim.exe"
!insertmacro MUI_PAGE_FINISH

; Uninstaller pages
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH

; Language
!insertmacro MUI_LANGUAGE "English"

Section "MainSection" SEC01
  SetOutPath "$INSTDIR"
  SetOverwrite ifnewer
  File /r "${DIST_DIR}\*.*"

  ; Create uninstaller
  WriteUninstaller "$INSTDIR\uninstall.exe"

  ; Start menu shortcuts
  CreateDirectory "$SMPROGRAMS\PxModRim"
  CreateShortcut "$SMPROGRAMS\PxModRim\PxModRim.lnk" "$INSTDIR\PxModRim.exe" "" "$INSTDIR\PxModRim.exe" 0
  CreateShortcut "$SMPROGRAMS\PxModRim\Uninstall PxModRim.lnk" "$INSTDIR\uninstall.exe" "" "$INSTDIR\uninstall.exe" 0

  ; Registry for App Paths
  WriteRegStr HKLM "${PRODUCT_DIR_REGKEY}" "" "$INSTDIR\PxModRim.exe"

  ; Registry for Add/Remove Programs
  WriteRegStr HKLM "${PRODUCT_UNINST_KEY}" "DisplayName" "${PRODUCT_NAME}"
  WriteRegStr HKLM "${PRODUCT_UNINST_KEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKLM "${PRODUCT_UNINST_KEY}" "Publisher" "${PRODUCT_PUBLISHER}"
  WriteRegStr HKLM "${PRODUCT_UNINST_KEY}" "URLInfoAbout" "${PRODUCT_WEB_SITE}"
  WriteRegStr HKLM "${PRODUCT_UNINST_KEY}" "DisplayIcon" "$INSTDIR\PxModRim.exe"
  WriteRegStr HKLM "${PRODUCT_UNINST_KEY}" "UninstallString" "$INSTDIR\uninstall.exe"
  WriteRegStr HKLM "${PRODUCT_UNINST_KEY}" "QuietUninstallString" "$INSTDIR\uninstall.exe /S"

  ; EstimatedSize in KB
  ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
  IntFmt $0 "0x%08X" $0
  WriteRegDWORD HKLM "${PRODUCT_UNINST_KEY}" "EstimatedSize" "$0"
SectionEnd

Section "Uninstall"
  Delete "$SMPROGRAMS\PxModRim\PxModRim.lnk"
  Delete "$SMPROGRAMS\PxModRim\Uninstall PxModRim.lnk"
  RMDir "$SMPROGRAMS\PxModRim"

  DeleteRegKey HKLM "${PRODUCT_UNINST_KEY}"
  DeleteRegKey HKLM "${PRODUCT_DIR_REGKEY}"

  RMDir /r "$INSTDIR"
SectionEnd
