!macro NSIS_HOOK_POSTUNINSTALL
  ${If} $UpdateMode <> 1
    RMDir /r "$LOCALAPPDATA\${BUNDLEID}\sidecars"
    RMDir "$LOCALAPPDATA\${BUNDLEID}"
  ${EndIf}
!macroend
