; Installateur Layers Split - By JB Praise - NSIS (Modern UI 2) - fichier UTF-8 avec BOM
Unicode true
!include "MUI2.nsh"
!include "x64.nsh"
!include "FileFunc.nsh"

!define APPNAME "Layers Split"
!ifndef APPVER
  !define APPVER "1.3.0"
!endif
!define PUBLISHER "JB Praise"
!define UNKEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\LayersSplit"
!define REGKEY "Software\JB Praise\Layers Split"

!ifndef PAYLOAD
  !define PAYLOAD "payload"
!endif

Name "${APPNAME}"
OutFile "Layers-Split-Setup-${APPVER}.exe"
RequestExecutionLevel admin
InstallDir "$PROGRAMFILES64\Layers Split"
InstallDirRegKey HKLM "${REGKEY}" "InstallDir"
BrandingText "By JB Praise - for church musician"
SetCompressor /SOLID lzma
SetCompressorDictSize 64
ShowInstDetails show
ShowUninstDetails show

VIProductVersion "${APPVER}.0"
VIAddVersionKey /LANG=1036 "ProductName" "Layers Split"
VIAddVersionKey /LANG=1036 "CompanyName" "JB Praise"
VIAddVersionKey /LANG=1036 "FileDescription" "Installateur de Layers Split"
VIAddVersionKey /LANG=1036 "FileVersion" "${APPVER}"
VIAddVersionKey /LANG=1036 "ProductVersion" "${APPVER}"
VIAddVersionKey /LANG=1036 "LegalCopyright" "© JB Praise"
VIAddVersionKey /LANG=1036 "OriginalFilename" "Layers-Split-Setup-${APPVER}.exe"

!define MUI_ICON "layerssplit.ico"
!define MUI_UNICON "layerssplit.ico"
!define MUI_WELCOMEFINISHPAGE_BITMAP "side.bmp"
!define MUI_UNWELCOMEFINISHPAGE_BITMAP "side.bmp"
!define MUI_HEADERIMAGE
!define MUI_HEADERIMAGE_BITMAP "header.bmp"
!define MUI_HEADERIMAGE_UNBITMAP "header.bmp"
!define MUI_ABORTWARNING

!define MUI_WELCOMEPAGE_TITLE "Bienvenue dans Layers Split"
!define MUI_WELCOMEPAGE_TEXT "Layers Split sépare un morceau en stems grâce à l'intelligence artificielle : voix lead, chœurs (et leurs pupitres soprano, alto, ténor, basse), batterie, basse, guitares (et guitare solo), piano / claviers et autres instruments. Il analyse aussi le tempo, la tonalité et le loudness.$\r$\n$\r$\nCet assistant va l'installer sur votre ordinateur, avec le plugin VST3 pour Ableton Live et les autres logiciels de musique.$\r$\n$\r$\nLa séparation fonctionne sans Internet. Une carte graphique (DirectX 12) la rend beaucoup plus rapide.$\r$\n$\r$\nBy JB Praise — for church musician."
!define MUI_COMPONENTSPAGE_SMALLDESC
!define MUI_FINISHPAGE_TITLE "Layers Split est installé"
!define MUI_FINISHPAGE_RUN
!define MUI_FINISHPAGE_RUN_TEXT "Lancer Layers Split"
!define MUI_FINISHPAGE_RUN_FUNCTION LancerAppli

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!define MUI_UNWELCOMEPAGE_TITLE "Désinstaller Layers Split"
!define MUI_UNWELCOMEPAGE_TEXT "Cet assistant va retirer Layers Split et son plugin VST3 de votre ordinateur.$\r$\n$\r$\nVos morceaux et vos stems exportés ne sont jamais touchés."
!insertmacro MUI_UNPAGE_WELCOME
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH

!insertmacro MUI_LANGUAGE "French"

LangString DESC_APP ${LANG_FRENCH} "Le logiciel Layers Split et son moteur IA (obligatoire)."
LangString DESC_VST ${LANG_FRENCH} "Le plugin VST3 pour Ableton Live et les autres logiciels de musique. Installé dans C:\Program Files\Common Files\VST3."

Function LancerAppli
  ; lance le logiciel avec les droits normaux de l'utilisateur, pas ceux de l'installateur
  Exec '"$WINDIR\explorer.exe" "$INSTDIR\Layers Split.exe"'
FunctionEnd

Function .onInit
  ${IfNot} ${RunningX64}
    MessageBox MB_ICONSTOP|MB_OK "Layers Split demande Windows 10 ou 11 en 64 bits." /SD IDOK
    Abort
  ${EndIf}
  SetRegView 64
  StrCpy $INSTDIR "$PROGRAMFILES64\Layers Split"
  ReadRegStr $0 HKLM "${REGKEY}" "InstallDir"
  ${If} $0 != ""
    StrCpy $INSTDIR $0
  ${EndIf}
FunctionEnd

Function FermerAppli
  ; ferme proprement le logiciel s'il est ouvert, puis de force s'il ne répond pas
  nsExec::Exec 'taskkill /IM "Layers Split.exe" /T'
  Pop $0
  Sleep 1500
  nsExec::Exec 'taskkill /F /IM "Layers Split.exe" /T'
  Pop $0
  Sleep 500
FunctionEnd

Section "Layers Split" SecApp
  SectionIn RO
  SetShellVarContext all
  DetailPrint "Fermeture de Layers Split s'il est ouvert..."
  Call FermerAppli

  SetOutPath "$INSTDIR"
  SetOverwrite on
  File "${PAYLOAD}\Layers Split.exe"
  File "${PAYLOAD}\onnxruntime.dll"
  File "${PAYLOAD}\DirectML.dll"
  File "${PAYLOAD}\Licences.txt"
  File "/oname=LayersSplit.ico" "layerssplit.ico"

  DetailPrint "Installation des modèles d'intelligence artificielle..."
  SetOutPath "$INSTDIR\models"
  File "${PAYLOAD}\models\*.onnx"
  File "${PAYLOAD}\models\*.json"
  SetOutPath "$INSTDIR"

  WriteUninstaller "$INSTDIR\Desinstaller.exe"

  ; raccourcis
  CreateDirectory "$SMPROGRAMS\Layers Split"
  Delete "$SMPROGRAMS\Layers Split\Layers Split.lnk"
  CreateShortcut "$SMPROGRAMS\Layers Split\Layers Split.lnk" "$INSTDIR\Layers Split.exe" "" "$INSTDIR\LayersSplit.ico" 0
  CreateShortcut "$SMPROGRAMS\Layers Split\Désinstaller Layers Split.lnk" "$INSTDIR\Desinstaller.exe" "" "$INSTDIR\LayersSplit.ico" 0
  CreateShortcut "$DESKTOP\Layers Split.lnk" "$INSTDIR\Layers Split.exe" "" "$INSTDIR\LayersSplit.ico" 0

  ; emplacement du moteur, lu par le plugin VST3
  WriteRegStr HKLM "${REGKEY}" "InstallDir" "$INSTDIR"
  WriteRegStr HKLM "${REGKEY}" "Version" "${APPVER}"

  ; « Applications installées » de Windows
  WriteRegStr HKLM "${UNKEY}" "DisplayName" "Layers Split"
  WriteRegStr HKLM "${UNKEY}" "DisplayVersion" "${APPVER}"
  WriteRegStr HKLM "${UNKEY}" "Publisher" "${PUBLISHER}"
  WriteRegStr HKLM "${UNKEY}" "DisplayIcon" "$INSTDIR\LayersSplit.ico"
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
  ; le paquet VST3 complet (binaire + description du module)
  RMDir /r "$COMMONFILES64\VST3\Layers Split.vst3"
  SetOutPath "$COMMONFILES64\VST3"
  SetOverwrite on
  File /r "${PAYLOAD}\Layers Split.vst3"
  WriteRegDWORD HKLM "${REGKEY}" "VST3" 1
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
  nsExec::Exec 'taskkill /F /IM "Layers Split.exe" /T'
  Pop $0
  Sleep 500

  Delete "$INSTDIR\Layers Split.exe"
  Delete "$INSTDIR\onnxruntime.dll"
  Delete "$INSTDIR\DirectML.dll"
  Delete "$INSTDIR\Licences.txt"
  Delete "$INSTDIR\LayersSplit.ico"
  Delete "$INSTDIR\models\*.onnx"
  Delete "$INSTDIR\models\*.json"
  RMDir "$INSTDIR\models"
  Delete "$INSTDIR\Desinstaller.exe"
  RMDir "$INSTDIR"

  RMDir /r "$COMMONFILES64\VST3\Layers Split.vst3"

  Delete "$SMPROGRAMS\Layers Split\Layers Split.lnk"
  Delete "$SMPROGRAMS\Layers Split\Désinstaller Layers Split.lnk"
  RMDir "$SMPROGRAMS\Layers Split"
  Delete "$DESKTOP\Layers Split.lnk"

  DeleteRegKey HKLM "${UNKEY}"
  DeleteRegKey HKLM "${REGKEY}"
  DeleteRegKey /ifempty HKLM "Software\JB Praise"

  ; stems déjà calculés (cache) : effacés seulement si l'utilisateur le demande
  SetShellVarContext current
  IfFileExists "$LOCALAPPDATA\LayersSplit\*.*" 0 fin
  MessageBox MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2 "Voulez-vous aussi effacer les stems déjà calculés (cache de Layers Split) ?$\r$\n$\r$\nDossier : $LOCALAPPDATA\LayersSplit$\r$\n$\r$\nChoisissez « Non » pour les garder : vos projets les retrouveront sans refaire la séparation." /SD IDNO IDNO fin
  RMDir /r "$LOCALAPPDATA\LayersSplit"
  fin:
SectionEnd
