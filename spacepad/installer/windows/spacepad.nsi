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
; choix de la langue au démarrage (proposé selon la langue de Windows), retenu pour la désinstallation
!define MUI_LANGDLL_REGISTRY_ROOT "HKLM"
!define MUI_LANGDLL_REGISTRY_KEY "Software\JB Praise\Spacepad"
!define MUI_LANGDLL_REGISTRY_VALUENAME "InstallerLanguage"
!define MUI_LANGDLL_ALLLANGUAGES

!define MUI_WELCOMEPAGE_TITLE "$(T_WELCOME_TITLE)"
!define MUI_WELCOMEPAGE_TEXT "$(T_WELCOME_TEXT)"
!define MUI_COMPONENTSPAGE_SMALLDESC
!define MUI_FINISHPAGE_TITLE "$(T_FINISH_TITLE)"
!define MUI_FINISHPAGE_TEXT "$(T_FINISH_TEXT)"
!define MUI_FINISHPAGE_RUN
!define MUI_FINISHPAGE_RUN_TEXT "$(T_RUN)"
!define MUI_FINISHPAGE_RUN_FUNCTION LancerAppli

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!define MUI_UNWELCOMEPAGE_TITLE "$(T_UN_TITLE)"
!define MUI_UNWELCOMEPAGE_TEXT "$(T_UN_TEXT)"
!insertmacro MUI_UNPAGE_WELCOME
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH

!insertmacro MUI_LANGUAGE "French"
!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "Spanish"
!insertmacro MUI_RESERVEFILE_LANGDLL

LangString T_WELCOME_TITLE ${LANG_FRENCH} "Bienvenue dans Spacepad"
LangString T_WELCOME_TITLE ${LANG_ENGLISH} "Welcome to Spacepad"
LangString T_WELCOME_TITLE ${LANG_SPANISH} "Bienvenido a Spacepad"
LangString T_WELCOME_TEXT ${LANG_FRENCH} "Spacepad est un lecteur de pads et de crowds pour la scène : choisissez le dossier de vos sons, Spacepad l'analyse, garde son nom, trouve la tonalité de chaque son et l'enregistre dans sa Pad Library ou sa Crowd Library.$\r$\n$\r$\nCet assistant installe l'application et le plugin VST3 pour Ableton Live, Cubase, FL Studio, Studio One et les autres logiciels de musique.$\r$\n$\r$\nSpacepad by JB Praise."
LangString T_WELCOME_TEXT ${LANG_ENGLISH} "Spacepad is a pad and crowd player for the stage: choose your sounds folder and Spacepad analyzes it, keeps its name, finds the key of every sound and saves it to its Pad Library or Crowd Library.$\r$\n$\r$\nThis wizard installs the app and the VST3 plugin for Ableton Live, Cubase, FL Studio, Studio One and other music software.$\r$\n$\r$\nSpacepad by JB Praise."
LangString T_WELCOME_TEXT ${LANG_SPANISH} "Spacepad es un reproductor de pads y crowds para el escenario: elige la carpeta de tus sonidos y Spacepad la analiza, conserva su nombre, encuentra la tonalidad de cada sonido y la guarda en su Pad Library o su Crowd Library.$\r$\n$\r$\nEste asistente instala la aplicación y el plugin VST3 para Ableton Live, Cubase, FL Studio, Studio One y otros programas de música.$\r$\n$\r$\nSpacepad by JB Praise."
LangString T_FINISH_TITLE ${LANG_FRENCH} "Spacepad est installé"
LangString T_FINISH_TITLE ${LANG_ENGLISH} "Spacepad is installed"
LangString T_FINISH_TITLE ${LANG_SPANISH} "Spacepad está instalado"
LangString T_FINISH_TEXT ${LANG_FRENCH} "Ouvrez Spacepad, cliquez sur « Choisir un dossier… » et sélectionnez le dossier de vos sons.$\r$\n$\r$\nUne fois enregistrées, vos librairies sont aussi reconnues dans le plugin VST3.$\r$\n$\r$\nSpacepad by JB Praise."
LangString T_FINISH_TEXT ${LANG_ENGLISH} "Open Spacepad, click “Choose a folder…” and select your sounds folder.$\r$\n$\r$\nOnce saved, your libraries are also recognized in the VST3 plugin.$\r$\n$\r$\nSpacepad by JB Praise."
LangString T_FINISH_TEXT ${LANG_SPANISH} "Abre Spacepad, haz clic en «Elegir una carpeta…» y selecciona la carpeta de tus sonidos.$\r$\n$\r$\nUna vez guardadas, tus librerías también se reconocen en el plugin VST3.$\r$\n$\r$\nSpacepad by JB Praise."
LangString T_RUN ${LANG_FRENCH} "Lancer Spacepad"
LangString T_RUN ${LANG_ENGLISH} "Launch Spacepad"
LangString T_RUN ${LANG_SPANISH} "Abrir Spacepad"
LangString T_UN_TITLE ${LANG_FRENCH} "Désinstaller Spacepad"
LangString T_UN_TITLE ${LANG_ENGLISH} "Uninstall Spacepad"
LangString T_UN_TITLE ${LANG_SPANISH} "Desinstalar Spacepad"
LangString T_UN_TEXT ${LANG_FRENCH} "Cet assistant va retirer Spacepad et son plugin VST3 de votre ordinateur.$\r$\n$\r$\nVos sons ne sont jamais effacés. La liste de vos librairies est conservée, sauf si vous demandez de l'effacer."
LangString T_UN_TEXT ${LANG_ENGLISH} "This wizard will remove Spacepad and its VST3 plugin from your computer.$\r$\n$\r$\nYour sounds are never deleted. Your library list is kept unless you ask to delete it."
LangString T_UN_TEXT ${LANG_SPANISH} "Este asistente quitará Spacepad y su plugin VST3 de tu ordenador.$\r$\n$\r$\nTus sonidos nunca se borran. La lista de tus librerías se conserva, salvo si pides borrarla."
LangString SEC_APP_NAME ${LANG_FRENCH} "Spacepad"
LangString SEC_APP_NAME ${LANG_ENGLISH} "Spacepad"
LangString SEC_APP_NAME ${LANG_SPANISH} "Spacepad"
LangString SEC_VST_NAME ${LANG_FRENCH} "Plugin VST3 (Ableton Live, Cubase, FL Studio…)"
LangString SEC_VST_NAME ${LANG_ENGLISH} "VST3 plugin (Ableton Live, Cubase, FL Studio…)"
LangString SEC_VST_NAME ${LANG_SPANISH} "Plugin VST3 (Ableton Live, Cubase, FL Studio…)"
LangString DESC_APP ${LANG_FRENCH} "Le logiciel Spacepad (obligatoire)."
LangString DESC_APP ${LANG_ENGLISH} "The Spacepad app (required)."
LangString DESC_APP ${LANG_SPANISH} "La aplicación Spacepad (obligatoria)."
LangString DESC_VST ${LANG_FRENCH} "Le plugin VST3 pour Ableton Live, Cubase, FL Studio, Studio One… Installé dans C:\Program Files\Common Files\VST3."
LangString DESC_VST ${LANG_ENGLISH} "The VST3 plugin for Ableton Live, Cubase, FL Studio, Studio One… Installed in C:\Program Files\Common Files\VST3."
LangString DESC_VST ${LANG_SPANISH} "El plugin VST3 para Ableton Live, Cubase, FL Studio, Studio One… Instalado en C:\Program Files\Common Files\VST3."
LangString T_NEED64 ${LANG_FRENCH} "Spacepad demande un Windows 64 bits."
LangString T_NEED64 ${LANG_ENGLISH} "Spacepad requires 64-bit Windows."
LangString T_NEED64 ${LANG_SPANISH} "Spacepad necesita Windows de 64 bits."
LangString T_CLOSING ${LANG_FRENCH} "Fermeture de Spacepad s'il est ouvert..."
LangString T_CLOSING ${LANG_ENGLISH} "Closing Spacepad if it is open..."
LangString T_CLOSING ${LANG_SPANISH} "Cerrando Spacepad si está abierto..."
LangString T_UNINSTALL_LNK ${LANG_FRENCH} "Désinstaller Spacepad"
LangString T_UNINSTALL_LNK ${LANG_ENGLISH} "Uninstall Spacepad"
LangString T_UNINSTALL_LNK ${LANG_SPANISH} "Desinstalar Spacepad"
LangString T_ASK_DATA ${LANG_FRENCH} "Voulez-vous aussi effacer la liste de vos librairies Spacepad ?$\r$\n$\r$\nVos fichiers de sons ne sont jamais effacés.$\r$\n$\r$\nChoisissez « Non » pour la conserver (recommandé si vous comptez réinstaller)."
LangString T_ASK_DATA ${LANG_ENGLISH} "Do you also want to delete your Spacepad library list?$\r$\n$\r$\nYour sound files are never deleted.$\r$\n$\r$\nChoose “No” to keep it (recommended if you plan to reinstall)."
LangString T_ASK_DATA ${LANG_SPANISH} "¿Quieres borrar también la lista de tus librerías de Spacepad?$\r$\n$\r$\nTus archivos de sonido nunca se borran.$\r$\n$\r$\nElige «No» para conservarla (recomendado si piensas reinstalar)."

Function LancerAppli
  ; lance le logiciel avec les droits normaux de l'utilisateur, pas ceux de l'installateur
  Exec '"$WINDIR\explorer.exe" "$INSTDIR\Spacepad.exe"'
FunctionEnd

Function .onInit
  !insertmacro MUI_LANGDLL_DISPLAY
  ${IfNot} ${RunningX64}
    MessageBox MB_OK|MB_ICONSTOP "$(T_NEED64)"
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

Section "$(SEC_APP_NAME)" SecApp
  SectionIn RO
  SetShellVarContext all
  DetailPrint "$(T_CLOSING)"
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
    ; langue de Spacepad = langue choisie dans l'installateur (seulement à la première installation)
    IfFileExists "$1\JB Praise\Spacepad\settings.json" langue_ok 0
    StrCpy $3 "fr"
    StrCmp $LANGUAGE ${LANG_ENGLISH} 0 +2
    StrCpy $3 "en"
    StrCmp $LANGUAGE ${LANG_SPANISH} 0 +2
    StrCpy $3 "es"
    FileOpen $4 "$1\JB Praise\Spacepad\settings.json" w
    FileWrite $4 '{"language": "$3"}'
    FileClose $4
    langue_ok:
  ${EndIf}

  ; raccourcis
  CreateDirectory "$SMPROGRAMS\Spacepad"
  CreateShortcut "$SMPROGRAMS\Spacepad\Spacepad.lnk" "$INSTDIR\Spacepad.exe" "" "$INSTDIR\Spacepad.ico" 0
  CreateShortcut "$SMPROGRAMS\Spacepad\$(T_UNINSTALL_LNK).lnk" "$INSTDIR\Desinstaller.exe" "" "$INSTDIR\Spacepad.ico" 0
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

Section "$(SEC_VST_NAME)" SecVST
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
  !insertmacro MUI_UNGETLANGUAGE
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
  Delete "$SMPROGRAMS\Spacepad\Uninstall Spacepad.lnk"
  Delete "$SMPROGRAMS\Spacepad\Desinstalar Spacepad.lnk"
  RMDir "$SMPROGRAMS\Spacepad"
  Delete "$DESKTOP\Spacepad.lnk"

  DeleteRegKey HKLM "${UNKEY}"
  DeleteRegKey HKLM "Software\JB Praise\Spacepad"
  DeleteRegKey /ifempty HKLM "Software\JB Praise"

  ; liste des librairies : jamais effacée sans demander (réponse par défaut « Non ») ; les sons eux-mêmes ne sont jamais touchés
  ReadEnvStr $1 "ProgramData"
  IfFileExists "$1\JB Praise\Spacepad\libraries.json" 0 fin
  MessageBox MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2 "$(T_ASK_DATA)" /SD IDNO IDNO fin
  RMDir /r "$1\JB Praise\Spacepad"
  RMDir "$1\JB Praise"
  fin:
SectionEnd
