; Installateur du Convertisseur STY (sty2audio) - By JB Praise - NSIS (Modern UI 2) - UTF-8 avec BOM
Unicode true
!include "MUI2.nsh"
!include "x64.nsh"
!include "FileFunc.nsh"

!define APPNAME "Convertisseur STY"
!define APPVER "1.0.0"
!define PUBLISHER "JB Praise"
!define UNKEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\ConvertisseurSTY"
!define REGKEY "Software\JB Praise\Convertisseur STY"
!define SHORT "Convertisseur STY (MP3 WAV)"
!define VERB "ConvertirSTYenMP3"

!ifndef PAYLOAD
  !define PAYLOAD "payload"
!endif

Name "${APPNAME}"
OutFile "Convertisseur-STY-Setup-${APPVER}.exe"
RequestExecutionLevel admin
InstallDir "$PROGRAMFILES64\Convertisseur STY"
InstallDirRegKey HKLM "${REGKEY}" "InstallDir"
BrandingText "By JB Praise - for church musician"
SetCompressor /SOLID lzma
ShowInstDetails show
ShowUninstDetails show

VIProductVersion "1.0.0.0"
VIAddVersionKey /LANG=1036 "ProductName" "Convertisseur STY"
VIAddVersionKey /LANG=1036 "CompanyName" "JB Praise"
VIAddVersionKey /LANG=1036 "FileDescription" "Installateur du Convertisseur STY vers MP3 et WAV"
VIAddVersionKey /LANG=1036 "FileVersion" "1.0.0"
VIAddVersionKey /LANG=1036 "ProductVersion" "1.0.0"
VIAddVersionKey /LANG=1036 "LegalCopyright" "© JB Praise"
VIAddVersionKey /LANG=1036 "OriginalFilename" "Convertisseur-STY-Setup-1.0.0.exe"

!define MUI_ICON "sty2audio.ico"
!define MUI_UNICON "sty2audio.ico"
!define MUI_WELCOMEFINISHPAGE_BITMAP "side.bmp"
!define MUI_UNWELCOMEFINISHPAGE_BITMAP "side.bmp"
!define MUI_HEADERIMAGE
!define MUI_HEADERIMAGE_BITMAP "header.bmp"
!define MUI_HEADERIMAGE_UNBITMAP "header.bmp"
!define MUI_ABORTWARNING

!define MUI_WELCOMEPAGE_TITLE "Bienvenue dans le Convertisseur STY"
!define MUI_WELCOMEPAGE_TEXT "Le Convertisseur STY transforme vos styles Yamaha (.sty, .prs, .bcs, .sst, .fps) en fichiers audio MP3 et WAV, section par section (Intro, Main, Fill In, Ending) et en enchaînement complet.$\r$\n$\r$\nLa banque de sons GeneralUser GS est incluse : aucun téléchargement n'est nécessaire.$\r$\n$\r$\nBy JB Praise — for church musician."
!define MUI_COMPONENTSPAGE_SMALLDESC
!define MUI_FINISHPAGE_TITLE "Le Convertisseur STY est installé"
!define MUI_FINISHPAGE_TEXT "Pour convertir :$\r$\n• double-cliquez sur « ${SHORT} » sur le Bureau et choisissez vos styles ;$\r$\n• ou glissez vos fichiers .sty sur cette icône ;$\r$\n• ou faites un clic droit sur un style → « Convertir en MP3 et WAV ».$\r$\n$\r$\nLes fichiers audio sont créés dans un dossier « nom du style (audio) » à côté du style."
!define MUI_FINISHPAGE_RUN
!define MUI_FINISHPAGE_RUN_TEXT "Lancer le Convertisseur STY"
!define MUI_FINISHPAGE_RUN_FUNCTION Lancer

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!define MUI_UNWELCOMEPAGE_TITLE "Désinstaller le Convertisseur STY"
!define MUI_UNWELCOMEPAGE_TEXT "Cet assistant va retirer le Convertisseur STY de votre ordinateur.$\r$\n$\r$\nVos styles et les fichiers audio déjà créés ne sont pas effacés."
!insertmacro MUI_UNPAGE_WELCOME
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH

!insertmacro MUI_LANGUAGE "French"

LangString DESC_APP ${LANG_FRENCH} "Le Convertisseur STY et la banque de sons GeneralUser GS (obligatoire)."
LangString DESC_MENU ${LANG_FRENCH} "Ajoute « Convertir en MP3 et WAV » au clic droit sur les styles Yamaha, et au menu « Envoyer vers »."
LangString DESC_DESK ${LANG_FRENCH} "Une icône sur le Bureau : double-clic pour choisir des styles, ou glisser-déposer des fichiers dessus."

Function Lancer
  ; lancé avec les droits normaux de l'utilisateur, pas ceux de l'installateur
  Exec '"$WINDIR\explorer.exe" "$INSTDIR\sty2audio.exe"'
FunctionEnd

Function .onInit
  ${If} ${RunningX64}
    SetRegView 64
    StrCpy $INSTDIR "$PROGRAMFILES64\Convertisseur STY"
    ReadRegStr $0 HKLM "${REGKEY}" "InstallDir"
    ${If} $0 != ""
      StrCpy $INSTDIR $0
    ${EndIf}
  ${Else}
    StrCpy $INSTDIR "$PROGRAMFILES\Convertisseur STY"
  ${EndIf}
FunctionEnd

!macro MenuExt EXT
  WriteRegStr HKLM "Software\Classes\SystemFileAssociations\${EXT}\shell\${VERB}" "" "Convertir en MP3 et WAV"
  WriteRegStr HKLM "Software\Classes\SystemFileAssociations\${EXT}\shell\${VERB}" "Icon" '"$INSTDIR\sty2audio.exe",0'
  WriteRegStr HKLM "Software\Classes\SystemFileAssociations\${EXT}\shell\${VERB}\command" "" '"$INSTDIR\sty2audio.exe" "%1"'
!macroend
!macro UnMenuExt EXT
  DeleteRegKey HKLM "Software\Classes\SystemFileAssociations\${EXT}\shell\${VERB}"
!macroend

Section "Convertisseur STY" SecApp
  SectionIn RO
  SetShellVarContext all
  nsExec::Exec 'taskkill /F /IM sty2audio.exe /T'
  SetOutPath "$INSTDIR"
  SetOverwrite on
  ${If} ${RunningX64}
    File "/oname=sty2audio.exe" "${PAYLOAD}\sty2audio.exe"
  ${Else}
    File "/oname=sty2audio.exe" "${PAYLOAD}\sty2audio32.exe"
  ${EndIf}
  File "${PAYLOAD}\GeneralUser-GS.sf2"
  File "/oname=GeneralUser-GS-licence.txt" "${PAYLOAD}\GeneralUser-GS-LICENSE.txt"
  File "/oname=Lisez-moi.txt" "${PAYLOAD}\Lisez-moi.txt"
  File "sty2audio.ico"
  WriteUninstaller "$INSTDIR\Desinstaller.exe"

  CreateDirectory "$SMPROGRAMS\Convertisseur STY"
  CreateShortcut "$SMPROGRAMS\Convertisseur STY\${SHORT}.lnk" "$INSTDIR\sty2audio.exe" "" "$INSTDIR\sty2audio.ico" 0
  CreateShortcut "$SMPROGRAMS\Convertisseur STY\Lisez-moi.lnk" "$INSTDIR\Lisez-moi.txt"
  CreateShortcut "$SMPROGRAMS\Convertisseur STY\Désinstaller le Convertisseur STY.lnk" "$INSTDIR\Desinstaller.exe" "" "$INSTDIR\sty2audio.ico" 0

  WriteRegStr HKLM "${REGKEY}" "InstallDir" "$INSTDIR"
  WriteRegStr HKLM "${UNKEY}" "DisplayName" "Convertisseur STY (MP3 WAV)"
  WriteRegStr HKLM "${UNKEY}" "DisplayVersion" "${APPVER}"
  WriteRegStr HKLM "${UNKEY}" "Publisher" "${PUBLISHER}"
  WriteRegStr HKLM "${UNKEY}" "DisplayIcon" "$INSTDIR\sty2audio.ico"
  WriteRegStr HKLM "${UNKEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKLM "${UNKEY}" "UninstallString" '"$INSTDIR\Desinstaller.exe"'
  WriteRegStr HKLM "${UNKEY}" "QuietUninstallString" '"$INSTDIR\Desinstaller.exe" /S'
  WriteRegDWORD HKLM "${UNKEY}" "NoModify" 1
  WriteRegDWORD HKLM "${UNKEY}" "NoRepair" 1
  ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
  IntFmt $0 "0x%08X" $0
  WriteRegDWORD HKLM "${UNKEY}" "EstimatedSize" "$0"
SectionEnd

Section "Clic droit « Convertir en MP3 et WAV »" SecMenu
  !insertmacro MenuExt ".sty"
  !insertmacro MenuExt ".prs"
  !insertmacro MenuExt ".bcs"
  !insertmacro MenuExt ".sst"
  !insertmacro MenuExt ".fps"
  !insertmacro MenuExt ".pcs"
  SetShellVarContext current
  CreateShortcut "$SENDTO\${SHORT}.lnk" "$INSTDIR\sty2audio.exe" "" "$INSTDIR\sty2audio.ico" 0
  SetShellVarContext all
SectionEnd

Section "Icône sur le Bureau" SecDesk
  SetShellVarContext all
  CreateShortcut "$DESKTOP\${SHORT}.lnk" "$INSTDIR\sty2audio.exe" "" "$INSTDIR\sty2audio.ico" 0
SectionEnd

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${SecApp} $(DESC_APP)
  !insertmacro MUI_DESCRIPTION_TEXT ${SecMenu} $(DESC_MENU)
  !insertmacro MUI_DESCRIPTION_TEXT ${SecDesk} $(DESC_DESK)
!insertmacro MUI_FUNCTION_DESCRIPTION_END

Function un.onInit
  ${If} ${RunningX64}
    SetRegView 64
  ${EndIf}
FunctionEnd

Section "Uninstall"
  SetShellVarContext all
  nsExec::Exec 'taskkill /F /IM sty2audio.exe /T'
  Sleep 300
  Delete "$INSTDIR\sty2audio.exe"
  Delete "$INSTDIR\GeneralUser-GS.sf2"
  Delete "$INSTDIR\GeneralUser-GS-licence.txt"
  Delete "$INSTDIR\Lisez-moi.txt"
  Delete "$INSTDIR\sty2audio.ico"
  Delete "$INSTDIR\Desinstaller.exe"
  RMDir "$INSTDIR"

  !insertmacro UnMenuExt ".sty"
  !insertmacro UnMenuExt ".prs"
  !insertmacro UnMenuExt ".bcs"
  !insertmacro UnMenuExt ".sst"
  !insertmacro UnMenuExt ".fps"
  !insertmacro UnMenuExt ".pcs"

  Delete "$SMPROGRAMS\Convertisseur STY\${SHORT}.lnk"
  Delete "$SMPROGRAMS\Convertisseur STY\Lisez-moi.lnk"
  Delete "$SMPROGRAMS\Convertisseur STY\Désinstaller le Convertisseur STY.lnk"
  RMDir "$SMPROGRAMS\Convertisseur STY"
  Delete "$DESKTOP\${SHORT}.lnk"
  SetShellVarContext current
  Delete "$SENDTO\${SHORT}.lnk"

  DeleteRegKey HKLM "${UNKEY}"
  DeleteRegKey HKLM "${REGKEY}"
  DeleteRegKey /ifempty HKLM "Software\JB Praise"
SectionEnd
