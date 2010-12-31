/*=========================================================================

  Program:   ParaView
  Module:    vtkSMUndoElement.cxx

  Copyright (c) Kitware, Inc.
  All rights reserved.
  See Copyright.txt or http://www.paraview.org/HTML/Copyright.html for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/
#include "vtkSMUndoElement.h"

#include "vtkObjectFactory.h"
#include "vtkSMSession.h"
#include "vtkSMProxyLocator.h"


vtkCxxSetObjectMacro(vtkSMUndoElement, Session, vtkSMSession);
//-----------------------------------------------------------------------------
vtkSMUndoElement::vtkSMUndoElement()
{
  this->Session = 0;
}

//-----------------------------------------------------------------------------
vtkSMUndoElement::~vtkSMUndoElement()
{
  this->SetSession(0);
}

//-----------------------------------------------------------------------------
void vtkSMUndoElement::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
}

