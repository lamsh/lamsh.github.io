' \file      PrintStyle.bas
' \author    SENOO, Ken
' \copyright CC0

Sub oWriterStyle
  Dim Dummy()
  oDoc = StarDesktop.loadComponentFromURL("private:factory/swriter", "_blank", 0, Dummy())

  'Get the StyleFamilies
  oFamilies = oDoc.StyleFamilies
  oFamilyNames = oFamilies.getElementNames()
  oStyleName = oFamilies.getByName("CharacterStyles")  ' ここでスタイル種類を指定
  oDisp = ""

  'Get the Style Name
  for i = 0 to oStyleName.Count - 1
    oDisp = oDisp & i & "," & oStyleName(i).Name & "," & oStyleName(i).DisplayName '' ここでAPI名と表示名を出力
    oDisp = oDisp & Chr$(10)
  next i

  msgbox(oDisp, 0, "Style Name")
End Sub
