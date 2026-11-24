; ZEBU Slicer - Windows installer (NSIS 3, Unicode)
;
; Built by packaging/windows/build_installer.ps1:
;   makensis /DSTAGE_DIR=<cmake --install prefix> /DVERSION=0.1.0-beta /DVERSION_NUM=0.1.0.0 /DOUT_FILE=<exe> zebu_installer.nsi
;
; - Language chosen at the start of the setup; ZEBU applies it at its next start (HKCU\Software\ZEBU Slicer\Language).
; - Default folder: Program Files\ZEBU Slicer, any folder (another drive...) can be chosen.
; - Updates: the app runs this installer with  /S /UPDATE  : silent, same folder (read from the registry),
;   waits for ZEBU to close, then restarts it as the normal user.

Unicode true
SetCompressor /SOLID lzma
ManifestDPIAware true
RequestExecutionLevel admin

!ifndef STAGE_DIR
  !error "STAGE_DIR (cmake --install prefix) is required"
!endif
!ifndef VERSION
  !define VERSION "0.1.0-beta"
!endif
!ifndef VERSION_NUM
  !define VERSION_NUM "0.1.0.0"
!endif
!ifndef OUT_FILE
  !define OUT_FILE "ZEBU_Slicer_Windows_Installer_V${VERSION}_x64.exe"
!endif

!define APP_NAME    "ZEBU Slicer"
!define APP_EXE     "zebu-slicer.exe"
!define REG_KEY     "Software\ZEBU Slicer"
!define UNINST_KEY  "Software\Microsoft\Windows\CurrentVersion\Uninstall\ZEBU Slicer"
!define ICON_FILE   "${STAGE_DIR}\resources\images\OrcaSlicer.ico"

Name "${APP_NAME} ${VERSION}"
OutFile "${OUT_FILE}"
InstallDir "$PROGRAMFILES64\${APP_NAME}"
InstallDirRegKey HKLM "${REG_KEY}" "InstallDir"
BrandingText "${APP_NAME} ${VERSION}"

VIProductVersion "${VERSION_NUM}"
VIAddVersionKey /LANG=0 "ProductName" "${APP_NAME}"
VIAddVersionKey /LANG=0 "ProductVersion" "${VERSION}"
VIAddVersionKey /LANG=0 "FileVersion" "${VERSION}"
VIAddVersionKey /LANG=0 "FileDescription" "${APP_NAME} installer"
VIAddVersionKey /LANG=0 "LegalCopyright" "AGPL-3.0 - fork of OrcaSlicer"

!include "MUI2.nsh"
!include "FileFunc.nsh"
!include "LogicLib.nsh"
!include "x64.nsh"

!define MUI_ICON   "${ICON_FILE}"
!define MUI_UNICON "${ICON_FILE}"
!define MUI_ABORTWARNING
; Remember the setup language for the next (interactive) install and for the uninstaller.
!define MUI_LANGDLL_REGISTRY_ROOT      HKCU
!define MUI_LANGDLL_REGISTRY_KEY       "${REG_KEY}"
!define MUI_LANGDLL_REGISTRY_VALUENAME "InstallerLanguage"
!define MUI_LANGDLL_ALLLANGUAGES

Var IsUpdate

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${STAGE_DIR}\LICENSE.txt"
!define MUI_PAGE_CUSTOMFUNCTION_LEAVE DirectoryLeave
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_RUN
!define MUI_FINISHPAGE_RUN_FUNCTION LaunchAsUser
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

; The first language is the default one. Keep this list in step with LangToWx below.
!insertmacro MUI_LANGUAGE "French"
!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "German"
!insertmacro MUI_LANGUAGE "Spanish"
!insertmacro MUI_LANGUAGE "Italian"
!insertmacro MUI_LANGUAGE "PortugueseBR"
!insertmacro MUI_LANGUAGE "Polish"
!insertmacro MUI_LANGUAGE "Dutch"
!insertmacro MUI_LANGUAGE "Czech"
!insertmacro MUI_LANGUAGE "Russian"
!insertmacro MUI_LANGUAGE "Ukrainian"
!insertmacro MUI_LANGUAGE "Turkish"
!insertmacro MUI_LANGUAGE "Japanese"
!insertmacro MUI_LANGUAGE "Korean"
!insertmacro MUI_LANGUAGE "SimpChinese"

!insertmacro MUI_RESERVEFILE_LANGDLL

; $LANGUAGE (Windows LANGID) -> wxWidgets canonical language name used by ZEBU ("language" setting).
Function LangToWx
  ${Select} $LANGUAGE
    ${Case} 1036
      StrCpy $0 "fr_FR"
    ${Case} 1031
      StrCpy $0 "de_DE"
    ${Case} 1034
      StrCpy $0 "es_ES"
    ${Case} 1040
      StrCpy $0 "it_IT"
    ${Case} 1046
      StrCpy $0 "pt_BR"
    ${Case} 1045
      StrCpy $0 "pl_PL"
    ${Case} 1043
      StrCpy $0 "nl_NL"
    ${Case} 1029
      StrCpy $0 "cs_CZ"
    ${Case} 1049
      StrCpy $0 "ru_RU"
    ${Case} 1058
      StrCpy $0 "uk_UA"
    ${Case} 1055
      StrCpy $0 "tr_TR"
    ${Case} 1041
      StrCpy $0 "ja_JP"
    ${Case} 1042
      StrCpy $0 "ko_KR"
    ${Case} 2052
      StrCpy $0 "zh_CN"
    ${CaseElse}
      StrCpy $0 "en"
  ${EndSelect}
FunctionEnd

; Never install straight into a drive root or a shared folder: the uninstaller removes the whole folder.
Function DirectoryLeave
  StrCpy $1 $INSTDIR 1 -1
  ${If} $1 == "\"
    StrCpy $INSTDIR $INSTDIR -1
  ${EndIf}
  StrCpy $1 $INSTDIR "" -11
  ${If} $1 != "${APP_NAME}"
    StrCpy $INSTDIR "$INSTDIR\${APP_NAME}"
  ${EndIf}
FunctionEnd

; Start ZEBU as the logged-in user, not with the installer's elevated rights.
Function LaunchAsUser
  Exec '"$WINDIR\explorer.exe" "$INSTDIR\${APP_EXE}"'
FunctionEnd

Function .onInit
  ${IfNot} ${RunningX64}
    MessageBox MB_ICONSTOP "ZEBU Slicer requires a 64-bit Windows."
    Abort
  ${EndIf}
  SetRegView 64
  StrCpy $IsUpdate "0"
  ${GetParameters} $R0
  ClearErrors
  ${GetOptions} $R0 "/UPDATE" $R1
  ${IfNot} ${Errors}
    StrCpy $IsUpdate "1"
  ${EndIf}
  ${IfNot} ${Silent}
    !insertmacro MUI_LANGDLL_DISPLAY
  ${EndIf}
FunctionEnd

Section "ZEBU Slicer" SecMain
  SectionIn RO
  SetShellVarContext all
  SetRegView 64

  ; Update: ZEBU was asked to quit just before; wait until its executable is free (max ~30 s).
  ${If} ${FileExists} "$INSTDIR\${APP_EXE}"
    StrCpy $2 0
    ${Do}
      ClearErrors
      Delete "$INSTDIR\${APP_EXE}.old"
      Rename "$INSTDIR\${APP_EXE}" "$INSTDIR\${APP_EXE}.old"
      ${IfNot} ${Errors}
        ${Break}
      ${EndIf}
      IntOp $2 $2 + 1
      ${If} $2 >= 60
        MessageBox MB_RETRYCANCEL|MB_ICONEXCLAMATION "Close ZEBU Slicer, then click Retry." /SD IDCANCEL IDRETRY retry
        Abort
        retry:
        StrCpy $2 0
      ${EndIf}
      Sleep 500
    ${Loop}
  ${EndIf}

  SetOutPath "$INSTDIR"
  ; Files of the previous version that are gone in this one must not linger (old DLLs, removed profiles).
  RMDir /r "$INSTDIR\resources"
  File /r "${STAGE_DIR}\*.*"
  Delete "$INSTDIR\${APP_EXE}.old"

  WriteUninstaller "$INSTDIR\uninstall.exe"

  WriteRegStr HKLM "${REG_KEY}" "InstallDir" "$INSTDIR"
  WriteRegStr HKLM "${REG_KEY}" "Version" "${VERSION}"
  WriteRegStr HKLM "${UNINST_KEY}" "DisplayName" "${APP_NAME}"
  WriteRegStr HKLM "${UNINST_KEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKLM "${UNINST_KEY}" "Publisher" "ZEBU Slicer"
  WriteRegStr HKLM "${UNINST_KEY}" "DisplayIcon" "$INSTDIR\${APP_EXE}"
  WriteRegStr HKLM "${UNINST_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKLM "${UNINST_KEY}" "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegStr HKLM "${UNINST_KEY}" "QuietUninstallString" '"$INSTDIR\uninstall.exe" /S'
  WriteRegDWORD HKLM "${UNINST_KEY}" "NoModify" 1
  WriteRegDWORD HKLM "${UNINST_KEY}" "NoRepair" 1
  ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
  WriteRegDWORD HKLM "${UNINST_KEY}" "EstimatedSize" $0

  ; Language picked in this setup, applied by ZEBU at its next start. A silent update keeps the user's choice.
  ${IfNot} ${Silent}
    Call LangToWx
    WriteRegStr HKCU "${REG_KEY}" "Language" $0
    System::Call 'kernel32::GetTickCount() i .r1'
    WriteRegStr HKCU "${REG_KEY}" "InstallStamp" "${VERSION}-$1"
  ${EndIf}

  CreateDirectory "$SMPROGRAMS\${APP_NAME}"
  CreateShortcut "$SMPROGRAMS\${APP_NAME}\${APP_NAME}.lnk" "$INSTDIR\${APP_EXE}"
  CreateShortcut "$SMPROGRAMS\${APP_NAME}\Uninstall ${APP_NAME}.lnk" "$INSTDIR\uninstall.exe"
  ${If} $IsUpdate == "0"
    CreateShortcut "$DESKTOP\${APP_NAME}.lnk" "$INSTDIR\${APP_EXE}"
  ${EndIf}

  ${If} $IsUpdate == "1"
    Call LaunchAsUser
  ${EndIf}
SectionEnd

Function un.onInit
  SetRegView 64
  !insertmacro MUI_UNGETLANGUAGE
FunctionEnd

Section "Uninstall"
  SetShellVarContext all
  SetRegView 64
  ; Safety: only remove a folder that really is a ZEBU installation.
  ${If} ${FileExists} "$INSTDIR\${APP_EXE}"
    RMDir /r "$INSTDIR"
  ${Else}
    Delete "$INSTDIR\uninstall.exe"
    RMDir "$INSTDIR"
  ${EndIf}
  Delete "$DESKTOP\${APP_NAME}.lnk"
  RMDir /r "$SMPROGRAMS\${APP_NAME}"
  DeleteRegKey HKLM "${UNINST_KEY}"
  DeleteRegKey HKLM "${REG_KEY}"
  ; User presets and settings (%APPDATA%\ZEBUSlicer) are kept on purpose.
SectionEnd
