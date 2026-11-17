; Installateur Spacepad - by JB Praise - NSIS (Modern UI 2) - fichier UTF-8 avec BOM
; Contenu attendu dans le dossier "payload" : Spacepad.exe (application) et Spacepad.vst3 (dossier du plugin VST3)
Unicode true
!include "MUI2.nsh"
!include "x64.nsh"
!include "FileFunc.nsh"

!define APPNAME "Spacepad"
!ifndef APPVER
  !define APPVER "1.0.0"
!endif
!define PUBLISHER "JB Praise"
!define UNKEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\Spacepad"

!ifndef PAYLOAD
  !define PAYLOAD "payload"
!endif

Name "${APPNAME}"
OutFile "Spacepad-Setup-${APPVER}.exe"
RequestExecutionLevel admin
InstallDir "$PROGRAMFILES64\Spacepad"
InstallDirRegKey HKLM "Software\JB Praise\Spacepad" "InstallDir"
BrandingText "Spacepad by JB Praise"
SetCompressor /SOLID lzma
ShowInstDetails show
ShowUninstDetails show

VIProductVersion "${APPVER}.0"
VIAddVersionKey /LANG=1036 "ProductName" "Spacepad"
VIAddVersionKey /LANG=1036 "CompanyName" "JB Praise"
VIAddVersionKey /LANG=1036 "FileDescription" "Installateur de Spacepad"
VIAddVersionKey /LANG=1036 "FileVersion" "${APPVER}"
VIAddVersionKey /LANG=1036 "ProductVersion" "${APPVER}"
VIAddVersionKey /LANG=1036 "LegalCopyright" "© JB Praise"
VIAddVersionKey /LANG=1036 "OriginalFilename" "Spacepad-Setup-${APPVER}.exe"

!define MUI_ICON "spacepad.ico"
!define MUI_UNICON "spacepad.ico"
!define MUI_WELCOMEFINISHPAGE_BITMAP "side.bmp"
!define MUI_UNWELCOMEFINISHPAGE_BITMAP "side.bmp"
!define MUI_HEADERIMAGE
!define MUI_HEADERIMAGE_BITMAP "header.bmp"
!define MUI_HEADERIMAGE_UNBITMAP "header.bmp"
!define MUI_ABORTWARNING

!define MUI_WELCOMEPAGE_TITLE "Bienvenue dans Spacepad"
!define MUI_WELCOMEPAGE_TEXT "Spacepad est un lecteur de pads et de crowds pour la scène : choisissez le dossier de vos sons, Spacepad l'analyse, garde son nom, trouve la tonalité de chaque son et l'enregistre dans sa Pad Library ou sa Crowd Library.$\r$\n$\r$\nCet assistant installe l'application et le plugin VST3 pour Ableton Live, Cubase, FL Studio, Studio One et les autres logiciels de musique.$\r$\n$\r$\nSpacepad by JB Praise."
!define MUI_COMPONENTSPAGE_SMALLDESC
!define MUI_FINISHPAGE_TITLE "Spacepad est installé"
!define MUI_FINISHPAGE_TEXT "Ouvrez Spacepad, cliquez sur « Choisir un dossier… » et sélectionnez le dossier de vos sons.$\r$\n$\r$\nUne fois enregistrées, vos librairies sont aussi reconnues dans le plugin VST3.$\r$\n$\r$\nSpacepad by JB Praise."
!define MUI_FINISHPAGE_RUN
!define MUI_FINISHPAGE_RUN_TEXT "Lancer Spacepad"
!define MUI_FINISHPAGE_RUN_FUNCTION LancerAppli

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!define MUI_UNWELCOMEPAGE_TITLE "Désinstaller Spacepad"
!define MUI_UNWELCOMEPAGE_TEXT "Cet assistant va retirer Spacepad et son plugin VST3 de votre ordinateur.$\r$\n$\r$\nVos sons ne sont jamais effacés. La liste de vos librairies est conservée, sauf si vous demandez de l'effacer."
!insertmacro MUI_UNPAGE_WELCOME
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH

!insertmacro MUI_LANGUAGE "French"

LangString DESC_APP ${LANG_FRENCH} "Le logiciel Spacepad (obligatoire)."
LangString DESC_VST ${LANG_FRENCH} "Le plugin VST3 pour Ableton Live, Cubase, FL Studio, Studio One… Installé dans C:\Program Files\Common Files\VST3."

Function LancerAppli
  ; lance le logiciel avec les droits normaux de l'utilisateur, pas ceux de l'installateur
  Exec '"$WINDIR\explorer.exe" "$INSTDIR\Spacepad.exe"'
FunctionEnd

Function .onInit
  ${IfNot} ${RunningX64}
    MessageBox MB_OK|MB_ICONSTOP "Spacepad demande un Windows 64 bits."
    Abort
  ${EndIf}
  SetRegView 64
  StrCpy $INSTDIR "$PROGRAMFILES64\Spacepad"
  ReadRegStr $0 HKLM "Software\JB Praise\Spacepad" "InstallDir"
  ${If} $0 != ""
    StrCpy $INSTDIR $0
  ${EndIf}
FunctionEnd

Function FermerAppli
  ; ferme proprement le logiciel s'il est ouvert, puis de force s'il ne répond pas
  nsExec::Exec 'taskkill /IM Spacepad.exe /T'
  Sleep 1500
  nsExec::Exec 'taskkill /F /IM Spacepad.exe /T'
  Sleep 500
FunctionEnd

Section "Spacepad" SecApp
  SectionIn RO
  SetShellVarContext all
  DetailPrint "Fermeture de Spacepad s'il est ouvert..."
  Call FermerAppli

  ; fichiers du programme (seulement les nôtres : les sons ne sont jamais touchés)
  SetOutPath "$INSTDIR"
  SetOverwrite on
  File "/oname=Spacepad.exe" "${PAYLOAD}\Spacepad.exe"
  File "/oname=Spacepad.ico" "spacepad.ico"
  WriteUninstaller "$INSTDIR\Desinstaller.exe"

  ; dossier commun des librairies : l'application et le VST3 lisent et écrivent la même liste
  ReadEnvStr $1 "ProgramData"
  ${If} $1 != ""
    CreateDirectory "$1\JB Praise\Spacepad"
    nsExec::Exec 'icacls "$1\JB Praise" /grant *S-1-5-32-545:(OI)(CI)M /T /C /Q'
    Pop $2
  ${EndIf}

  ; raccourcis
  CreateDirectory "$SMPROGRAMS\Spacepad"
  CreateShortcut "$SMPROGRAMS\Spacepad\Spacepad.lnk" "$INSTDIR\Spacepad.exe" "" "$INSTDIR\Spacepad.ico" 0
  CreateShortcut "$SMPROGRAMS\Spacepad\Désinstaller Spacepad.lnk" "$INSTDIR\Desinstaller.exe" "" "$INSTDIR\Spacepad.ico" 0
  CreateShortcut "$DESKTOP\Spacepad.lnk" "$INSTDIR\Spacepad.exe" "" "$INSTDIR\Spacepad.ico" 0

  ; « Applications installées » de Windows
  WriteRegStr HKLM "Software\JB Praise\Spacepad" "InstallDir" "$INSTDIR"
  WriteRegStr HKLM "${UNKEY}" "DisplayName" "Spacepad"
  WriteRegStr HKLM "${UNKEY}" "DisplayVersion" "${APPVER}"
  WriteRegStr HKLM "${UNKEY}" "Publisher" "${PUBLISHER}"
  WriteRegStr HKLM "${UNKEY}" "DisplayIcon" "$INSTDIR\Spacepad.ico"
  WriteRegStr HKLM "${UNKEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKLM "${UNKEY}" "UninstallString" '"$INSTDIR\Desinstaller.exe"'
  WriteRegStr HKLM "${UNKEY}" "QuietUninstallString" '"$INSTDIR\Desinstaller.exe" /S'
  WriteRegDWORD HKLM "${UNKEY}" "NoModify" 1
  WriteRegDWORD HKLM "${UNKEY}" "NoRepair" 1
  ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
  IntFmt $0 "0x%08X" $0
  WriteRegDWORD HKLM "${UNKEY}" "EstimatedSize" "$0"
SectionEnd

Section "Plugin VST3 (Ableton Live, Cubase, FL Studio…)" SecVST
  RMDir /r "$COMMONFILES64\VST3\Spacepad.vst3"
  SetOutPath "$COMMONFILES64\VST3"
  SetOverwrite on
  File /r "${PAYLOAD}\Spacepad.vst3"
  WriteRegDWORD HKLM "Software\JB Praise\Spacepad" "VST3" 1
SectionEnd

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${SecApp} $(DESC_APP)
  !insertmacro MUI_DESCRIPTION_TEXT ${SecVST} $(DESC_VST)
!insertmacro MUI_FUNCTION_DESCRIPTION_END

Function un.onInit
  SetRegView 64
FunctionEnd

Section "Uninstall"
  SetShellVarContext all
  nsExec::Exec 'taskkill /F /IM Spacepad.exe /T'
  Sleep 500
  Delete "$INSTDIR\Spacepad.exe"
  Delete "$INSTDIR\Spacepad.ico"
  Delete "$INSTDIR\Desinstaller.exe"
  RMDir "$INSTDIR"

  RMDir /r "$COMMONFILES64\VST3\Spacepad.vst3"

  Delete "$SMPROGRAMS\Spacepad\Spacepad.lnk"
  Delete "$SMPROGRAMS\Spacepad\Désinstaller Spacepad.lnk"
  RMDir "$SMPROGRAMS\Spacepad"
  Delete "$DESKTOP\Spacepad.lnk"

  DeleteRegKey HKLM "${UNKEY}"
  DeleteRegKey HKLM "Software\JB Praise\Spacepad"
  DeleteRegKey /ifempty HKLM "Software\JB Praise"

  ; liste des librairies : jamais effacée sans demander (réponse par défaut « Non ») ; les sons eux-mêmes ne sont jamais touchés
  ReadEnvStr $1 "ProgramData"
  IfFileExists "$1\JB Praise\Spacepad\libraries.json" 0 fin
  MessageBox MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2 "Voulez-vous aussi effacer la liste de vos librairies Spacepad ?$\r$\n$\r$\nVos fichiers de sons ne sont jamais effacés.$\r$\n$\r$\nChoisissez « Non » pour la conserver (recommandé si vous comptez réinstaller)." /SD IDNO IDNO fin
  RMDir /r "$1\JB Praise\Spacepad"
  RMDir "$1\JB Praise"
  fin:
SectionEnd
