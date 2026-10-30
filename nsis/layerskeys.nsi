; Installateur Layers Keys - By JB Praise - NSIS (Modern UI 2) - fichier UTF-8 avec BOM
Unicode true
!include "MUI2.nsh"
!include "x64.nsh"
!include "FileFunc.nsh"

!define APPNAME "Layers Keys"
!define APPVER "1.0.0"
!define PUBLISHER "JB Praise"
!define UNKEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\LayersKeys"
!define OLDUNKEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\LayersKeys"

!ifndef PAYLOAD
  !define PAYLOAD "payload"
!endif

Name "${APPNAME}"
OutFile "Layers-Keys-Setup-${APPVER}.exe"
RequestExecutionLevel admin
InstallDir "$PROGRAMFILES64\Layers Keys"
InstallDirRegKey HKLM "Software\JB Praise\Layers Keys" "InstallDir"
BrandingText "By JB Praise - for church musician"
SetCompressor /SOLID lzma
ShowInstDetails show
ShowUninstDetails show

VIProductVersion "1.0.0.0"
VIAddVersionKey /LANG=1036 "ProductName" "Layers Keys"
VIAddVersionKey /LANG=1036 "CompanyName" "JB Praise"
VIAddVersionKey /LANG=1036 "FileDescription" "Installateur de Layers Keys"
VIAddVersionKey /LANG=1036 "FileVersion" "1.0.0"
VIAddVersionKey /LANG=1036 "ProductVersion" "1.0.0"
VIAddVersionKey /LANG=1036 "LegalCopyright" "© JB Praise"
VIAddVersionKey /LANG=1036 "OriginalFilename" "Layers-Keys-Setup-1.0.0.exe"

!define MUI_ICON "layerskeys.ico"
!define MUI_UNICON "layerskeys.ico"
!define MUI_WELCOMEFINISHPAGE_BITMAP "side.bmp"
!define MUI_UNWELCOMEFINISHPAGE_BITMAP "side.bmp"
!define MUI_HEADERIMAGE
!define MUI_HEADERIMAGE_BITMAP "header.bmp"
!define MUI_HEADERIMAGE_UNBITMAP "header.bmp"
!define MUI_ABORTWARNING

!define MUI_WELCOMEPAGE_TITLE "Bienvenue dans Layers Keys"
!define MUI_WELCOMEPAGE_TEXT "Layers Keys est un piano et sampler à couches pour musiciens d'église : jusqu'à 8 sons superposés, SoundFont (SF2), WAV et MP3, presets, reverb et clavier MIDI.$\r$\n$\r$\nCet assistant va l'installer sur votre ordinateur, avec le plugin VST3 pour Ableton et les autres logiciels de musique.$\r$\n$\r$\nBy JB Praise — for church musician."
!define MUI_COMPONENTSPAGE_SMALLDESC
!define MUI_FINISHPAGE_TITLE "Layers Keys est installé"
!define MUI_FINISHPAGE_RUN
!define MUI_FINISHPAGE_RUN_TEXT "Lancer Layers Keys"
!define MUI_FINISHPAGE_RUN_FUNCTION LancerAppli

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!define MUI_UNWELCOMEPAGE_TITLE "Désinstaller Layers Keys"
!define MUI_UNWELCOMEPAGE_TEXT "Cet assistant va retirer Layers Keys et son plugin VST3 de votre ordinateur.$\r$\n$\r$\nVos sons, presets et projets ne sont pas effacés sans votre accord."
!insertmacro MUI_UNPAGE_WELCOME
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH

!insertmacro MUI_LANGUAGE "French"

LangString DESC_APP ${LANG_FRENCH} "Le logiciel Layers Keys (obligatoire)."
LangString DESC_VST ${LANG_FRENCH} "Le plugin VST3 pour Ableton Live et les autres logiciels de musique. Installé dans C:\Program Files\Common Files\VST3."

Function LancerAppli
  ; lance le logiciel avec les droits normaux de l'utilisateur, pas ceux de l'installateur
  Exec '"$WINDIR\explorer.exe" "$INSTDIR\LayersKeys.exe"'
FunctionEnd

Function .onInit
  ${If} ${RunningX64}
    SetRegView 64
    StrCpy $INSTDIR "$PROGRAMFILES64\Layers Keys"
    ReadRegStr $0 HKLM "Software\JB Praise\Layers Keys" "InstallDir"
    ${If} $0 != ""
      StrCpy $INSTDIR $0
    ${EndIf}
  ${Else}
    StrCpy $INSTDIR "$PROGRAMFILES\Layers Keys"
  ${EndIf}
FunctionEnd

Function FermerAppli
  ; ferme proprement le logiciel s'il est ouvert, puis de force s'il ne répond pas
  nsExec::Exec 'taskkill /IM LayersKeys.exe /T'
  nsExec::Exec 'taskkill /IM LayersKeys64.exe /T'
  nsExec::Exec 'taskkill /IM LayersKeys32.exe /T'
  Sleep 1500
  nsExec::Exec 'taskkill /F /IM LayersKeys.exe /T'
  nsExec::Exec 'taskkill /F /IM LayersKeys64.exe /T'
  nsExec::Exec 'taskkill /F /IM LayersKeys32.exe /T'
  Sleep 500
FunctionEnd

Section "Layers Keys" SecApp
  SectionIn RO
  SetShellVarContext all
  DetailPrint "Fermeture de Layers Keys s'il est ouvert..."
  Call FermerAppli

  ; ancienne version installée dans le profil utilisateur (ancien installateur) : retirée proprement
  ReadRegStr $1 HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\LayersKeys" "InstallLocation"
  ${If} $1 != ""
    DetailPrint "Remplacement de l'ancienne installation..."
    Delete "$1\LayersKeys.exe"
    Delete "$1\LayersKeys64.exe"
    Delete "$1\LayersKeys32.exe"
    Delete "$1\Désinstaller.exe"
    Delete "$1\unins.exe"
    RMDir "$1"
    DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\LayersKeys"
  ${EndIf}
  Delete "$DESKTOP\Layers Keys.lnk"
  Delete "$SMPROGRAMS\Layers Keys.lnk"

  ; fichiers du programme (seulement les nôtres : les sons, presets et projets ne sont jamais touchés)
  Delete "$INSTDIR\LayersKeys64.exe"
  Delete "$INSTDIR\LayersKeys32.exe"
  SetOutPath "$INSTDIR"
  SetOverwrite on
  ${If} ${RunningX64}
    File "/oname=LayersKeys.exe" "${PAYLOAD}\LayersKeys64.exe"
  ${Else}
    File "/oname=LayersKeys.exe" "${PAYLOAD}\LayersKeys32.exe"
  ${EndIf}
  File "/oname=LayersKeys.ico" "layerskeys.ico"
  WriteUninstaller "$INSTDIR\Desinstaller.exe"

  ; raccourcis
  CreateDirectory "$SMPROGRAMS\Layers Keys"
  Delete "$SMPROGRAMS\Layers Keys\Layers Keys.lnk"
  CreateShortcut "$SMPROGRAMS\Layers Keys\Layers Keys.lnk" "$INSTDIR\LayersKeys.exe" "" "$INSTDIR\LayersKeys.ico" 0
  CreateShortcut "$SMPROGRAMS\Layers Keys\Désinstaller Layers Keys.lnk" "$INSTDIR\Desinstaller.exe" "" "$INSTDIR\LayersKeys.ico" 0
  CreateShortcut "$DESKTOP\Layers Keys.lnk" "$INSTDIR\LayersKeys.exe" "" "$INSTDIR\LayersKeys.ico" 0

  ; « Applications installées » de Windows
  WriteRegStr HKLM "Software\JB Praise\Layers Keys" "InstallDir" "$INSTDIR"
  WriteRegStr HKLM "${UNKEY}" "DisplayName" "Layers Keys"
  WriteRegStr HKLM "${UNKEY}" "DisplayVersion" "${APPVER}"
  WriteRegStr HKLM "${UNKEY}" "Publisher" "${PUBLISHER}"
  WriteRegStr HKLM "${UNKEY}" "DisplayIcon" "$INSTDIR\LayersKeys.ico"
  WriteRegStr HKLM "${UNKEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKLM "${UNKEY}" "UninstallString" '"$INSTDIR\Desinstaller.exe"'
  WriteRegStr HKLM "${UNKEY}" "QuietUninstallString" '"$INSTDIR\Desinstaller.exe" /S'
  WriteRegDWORD HKLM "${UNKEY}" "NoModify" 1
  WriteRegDWORD HKLM "${UNKEY}" "NoRepair" 1
  ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
  IntFmt $0 "0x%08X" $0
  WriteRegDWORD HKLM "${UNKEY}" "EstimatedSize" "$0"
SectionEnd

Section "Plugin VST3 (Ableton Live et autres)" SecVST
  ${If} ${RunningX64}
    SetOutPath "$COMMONFILES64\VST3\Layers Keys.vst3\Contents\x86_64-win"
    SetOverwrite try
    File "/oname=Layers Keys.vst3" "${PAYLOAD}\LayersKeys.vst3"
    WriteRegDWORD HKLM "Software\JB Praise\Layers Keys" "VST3" 1
  ${Else}
    DetailPrint "Le plugin VST3 demande un Windows 64 bits : ignoré."
  ${EndIf}
SectionEnd

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${SecApp} $(DESC_APP)
  !insertmacro MUI_DESCRIPTION_TEXT ${SecVST} $(DESC_VST)
!insertmacro MUI_FUNCTION_DESCRIPTION_END

Function un.onInit
  ${If} ${RunningX64}
    SetRegView 64
  ${EndIf}
FunctionEnd

Section "Uninstall"
  SetShellVarContext all
  nsExec::Exec 'taskkill /F /IM LayersKeys.exe /T'
  Sleep 500
  Delete "$INSTDIR\LayersKeys.exe"
  Delete "$INSTDIR\LayersKeys.ico"
  Delete "$INSTDIR\Desinstaller.exe"
  RMDir "$INSTDIR"

  Delete "$COMMONFILES64\VST3\Layers Keys.vst3\Contents\x86_64-win\Layers Keys.vst3"
  RMDir "$COMMONFILES64\VST3\Layers Keys.vst3\Contents\x86_64-win"
  RMDir "$COMMONFILES64\VST3\Layers Keys.vst3\Contents"
  RMDir "$COMMONFILES64\VST3\Layers Keys.vst3"

  Delete "$SMPROGRAMS\Layers Keys\Layers Keys.lnk"
  Delete "$SMPROGRAMS\Layers Keys\Désinstaller Layers Keys.lnk"
  RMDir "$SMPROGRAMS\Layers Keys"
  Delete "$DESKTOP\Layers Keys.lnk"

  DeleteRegKey HKLM "${UNKEY}"
  DeleteRegKey HKLM "Software\JB Praise\Layers Keys"
  DeleteRegKey /ifempty HKLM "Software\JB Praise"

  ; réglages, sons et presets : jamais effacés sans demander (la réponse par défaut est « Non »)
  SetShellVarContext current
  IfFileExists "$APPDATA\LayersKeys\*.*" 0 fin
  MessageBox MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2 "Voulez-vous aussi effacer vos réglages, sons et presets Layers Keys ?$\r$\n$\r$\nDossier : $APPDATA\LayersKeys$\r$\n$\r$\nChoisissez « Non » pour les conserver (recommandé si vous comptez réinstaller)." /SD IDNO IDNO fin
  RMDir /r "$APPDATA\LayersKeys"
  fin:
SectionEnd
