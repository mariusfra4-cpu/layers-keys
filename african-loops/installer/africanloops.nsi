; Installateur African Loops Pro - NSIS (Modern UI 2) - fichier UTF-8 avec BOM
Unicode true
!include "MUI2.nsh"
!include "x64.nsh"
!include "FileFunc.nsh"
!include "LogicLib.nsh"

!define APPNAME "African Loops Pro"
!define APPVER "1.0.0"
!define PUBLISHER "JB Praise"
!define EXENAME "African Loops Pro.exe"
!define UNKEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\AfricanLoopsPro"
!define REGKEY "Software\JB Praise\African Loops Pro"

!ifndef PAYLOAD
  !define PAYLOAD "payload"
!endif

Name "${APPNAME}"
OutFile "African-Loops-Pro-Setup-${APPVER}.exe"
RequestExecutionLevel admin
InstallDir "$PROGRAMFILES64\African Loops Pro"
InstallDirRegKey HKLM "${REGKEY}" "InstallDir"
BrandingText "African Loops Pro - By JB Praise"
SetCompressor /SOLID lzma
ShowInstDetails show
ShowUninstDetails show

VIProductVersion "1.0.0.0"
VIAddVersionKey /LANG=1036 "ProductName" "African Loops Pro"
VIAddVersionKey /LANG=1036 "CompanyName" "JB Praise"
VIAddVersionKey /LANG=1036 "FileDescription" "Installateur d'African Loops Pro"
VIAddVersionKey /LANG=1036 "FileVersion" "1.0.0"
VIAddVersionKey /LANG=1036 "ProductVersion" "1.0.0"
VIAddVersionKey /LANG=1036 "LegalCopyright" "© JB Praise"
VIAddVersionKey /LANG=1036 "OriginalFilename" "African-Loops-Pro-Setup-1.0.0.exe"

!define MUI_ICON "africanloops.ico"
!define MUI_UNICON "africanloops.ico"
!define MUI_WELCOMEFINISHPAGE_BITMAP "side.bmp"
!define MUI_UNWELCOMEFINISHPAGE_BITMAP "side.bmp"
!define MUI_HEADERIMAGE
!define MUI_HEADERIMAGE_BITMAP "header.bmp"
!define MUI_HEADERIMAGE_UNBITMAP "header.bmp"
!define MUI_ABORTWARNING

!define MUI_WELCOMEPAGE_TITLE "Bienvenue dans African Loops Pro"
!define MUI_WELCOMEPAGE_TEXT "African Loops Pro joue vos loops africaines (WAV et MP3) en boucle, sur 8 pistes : tempo trouvé automatiquement, percussions reconnues et séparées (kick, caisse claire, hi-hat, djembé, talking drum), bibliothèque classée par style.$\r$\n$\r$\nCet assistant va l'installer sur votre ordinateur, avec le plugin VST3 pour FL Studio, Ableton et les autres logiciels de musique.$\r$\n$\r$\nBy JB Praise."
!define MUI_COMPONENTSPAGE_SMALLDESC
!define MUI_FINISHPAGE_TITLE "African Loops Pro est installé"
!define MUI_FINISHPAGE_TEXT "African Loops Pro est prêt.$\r$\n$\r$\nDans votre logiciel de musique, lancez une recherche de nouveaux plugins si besoin : African Loops Pro apparaît dans les instruments."
!define MUI_FINISHPAGE_RUN
!define MUI_FINISHPAGE_RUN_TEXT "Lancer African Loops Pro"
!define MUI_FINISHPAGE_RUN_FUNCTION LancerAppli

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!define MUI_UNWELCOMEPAGE_TITLE "Désinstaller African Loops Pro"
!define MUI_UNWELCOMEPAGE_TEXT "Cet assistant va retirer African Loops Pro et son plugin VST3 de votre ordinateur.$\r$\n$\r$\nVos loops ne sont jamais effacées."
!insertmacro MUI_UNPAGE_WELCOME
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH

!insertmacro MUI_LANGUAGE "French"

LangString DESC_APP ${LANG_FRENCH} "Le logiciel African Loops Pro, utilisable seul (obligatoire)."
LangString DESC_VST ${LANG_FRENCH} "Le plugin VST3 pour FL Studio, Ableton Live, Cubase, Studio One, Reaper... Installé dans C:\Program Files\Common Files\VST3."

Function LancerAppli
  ; lance le logiciel avec les droits normaux de l'utilisateur, pas ceux de l'installateur
  Exec '"$WINDIR\explorer.exe" "$INSTDIR\${EXENAME}"'
FunctionEnd

Function .onInit
  ${IfNot} ${RunningX64}
    MessageBox MB_ICONSTOP "African Loops Pro demande un Windows 64 bits."
    Abort
  ${EndIf}
  SetRegView 64
  StrCpy $INSTDIR "$PROGRAMFILES64\African Loops Pro"
  ReadRegStr $0 HKLM "${REGKEY}" "InstallDir"
  ${If} $0 != ""
    StrCpy $INSTDIR $0
  ${EndIf}
FunctionEnd

Function FermerAppli
  ; ferme proprement le logiciel s'il est ouvert, puis de force s'il ne répond pas
  nsExec::Exec 'taskkill /IM "${EXENAME}" /T'
  Pop $0
  Sleep 1500
  nsExec::Exec 'taskkill /F /IM "${EXENAME}" /T'
  Pop $0
  Sleep 500
FunctionEnd

Section "African Loops Pro" SecApp
  SectionIn RO
  SetShellVarContext all
  DetailPrint "Fermeture d'African Loops Pro s'il est ouvert..."
  Call FermerAppli

  SetOutPath "$INSTDIR"
  SetOverwrite on
  File "${PAYLOAD}\${EXENAME}"
  File "africanloops.ico"
  File "/oname=Lisez-moi.txt" "${PAYLOAD}\INSTALLER.txt"
  WriteUninstaller "$INSTDIR\Desinstaller.exe"

  ; raccourcis
  CreateDirectory "$SMPROGRAMS\African Loops Pro"
  CreateShortcut "$SMPROGRAMS\African Loops Pro\African Loops Pro.lnk" "$INSTDIR\${EXENAME}" "" "$INSTDIR\africanloops.ico" 0
  CreateShortcut "$SMPROGRAMS\African Loops Pro\Désinstaller African Loops Pro.lnk" "$INSTDIR\Desinstaller.exe" "" "$INSTDIR\africanloops.ico" 0
  CreateShortcut "$DESKTOP\African Loops Pro.lnk" "$INSTDIR\${EXENAME}" "" "$INSTDIR\africanloops.ico" 0

  ; « Applications installées » de Windows
  WriteRegStr HKLM "${REGKEY}" "InstallDir" "$INSTDIR"
  WriteRegStr HKLM "${UNKEY}" "DisplayName" "African Loops Pro"
  WriteRegStr HKLM "${UNKEY}" "DisplayVersion" "${APPVER}"
  WriteRegStr HKLM "${UNKEY}" "Publisher" "${PUBLISHER}"
  WriteRegStr HKLM "${UNKEY}" "DisplayIcon" "$INSTDIR\africanloops.ico"
  WriteRegStr HKLM "${UNKEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKLM "${UNKEY}" "UninstallString" '"$INSTDIR\Desinstaller.exe"'
  WriteRegStr HKLM "${UNKEY}" "QuietUninstallString" '"$INSTDIR\Desinstaller.exe" /S'
  WriteRegDWORD HKLM "${UNKEY}" "NoModify" 1
  WriteRegDWORD HKLM "${UNKEY}" "NoRepair" 1
SectionEnd

Section "Plugin VST3 (FL Studio, Ableton et autres)" SecVST
  ; le plugin est un dossier « African Loops Pro.vst3 » : l'ancienne version est d'abord retirée
  DetailPrint "Installation du plugin VST3..."
  RMDir /r "$COMMONFILES64\VST3\African Loops Pro.vst3"
  SetOutPath "$COMMONFILES64\VST3"
  SetOverwrite on
  File /r "${PAYLOAD}\African Loops Pro.vst3"
  WriteRegDWORD HKLM "${REGKEY}" "VST3" 1
SectionEnd

Section "-Taille"
  ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
  ${GetSize} "$COMMONFILES64\VST3\African Loops Pro.vst3" "/S=0K" $3 $1 $2
  IntOp $0 $0 + $3
  IntFmt $0 "0x%08X" $0
  WriteRegDWORD HKLM "${UNKEY}" "EstimatedSize" "$0"
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
  nsExec::Exec 'taskkill /F /IM "${EXENAME}" /T'
  Pop $0
  Sleep 500
  Delete "$INSTDIR\${EXENAME}"
  Delete "$INSTDIR\africanloops.ico"
  Delete "$INSTDIR\Lisez-moi.txt"
  Delete "$INSTDIR\Desinstaller.exe"
  RMDir "$INSTDIR"

  RMDir /r "$COMMONFILES64\VST3\African Loops Pro.vst3"

  Delete "$SMPROGRAMS\African Loops Pro\African Loops Pro.lnk"
  Delete "$SMPROGRAMS\African Loops Pro\Désinstaller African Loops Pro.lnk"
  RMDir "$SMPROGRAMS\African Loops Pro"
  Delete "$DESKTOP\African Loops Pro.lnk"

  DeleteRegKey HKLM "${UNKEY}"
  DeleteRegKey HKLM "${REGKEY}"
  DeleteRegKey /ifempty HKLM "Software\JB Praise"

  ; fichiers temporaires du glisser-déposer (vos loops ne sont jamais touchées)
  SetShellVarContext current
  RMDir /r "$TEMP\African Loops"
SectionEnd
