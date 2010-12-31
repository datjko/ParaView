/*=========================================================================

  Program:   ParaView
  Module:    vtkSMNewWidgetRepresentationProxy.cxx

  Copyright (c) Kitware, Inc.
  All rights reserved.
  See Copyright.txt or http://www.paraview.org/HTML/Copyright.html for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/
#include "vtkSMNewWidgetRepresentationProxy.h"

#include "vtk3DWidgetRepresentation.h"
#include "vtkAbstractWidget.h"
#include "vtkClientServerInterpreter.h"
#include "vtkClientServerStream.h"
#include "vtkCommand.h"
#include "vtkObjectFactory.h"
#include "vtkPMProxy.h"
#include "vtkProcessModule.h"
#include "vtkPVGenericRenderWindowInteractor.h"
#include "vtkSmartPointer.h"
#include "vtkSMDoubleVectorProperty.h"
#include "vtkSMIntVectorProperty.h"
#include "vtkSMMessage.h"
#include "vtkSMPropertyIterator.h"
#include "vtkSMPropertyLink.h"
#include "vtkSMProxyProperty.h"
#include "vtkSMRenderViewProxy.h"
#include "vtkSMSession.h"
#include "vtkSMViewProxy.h"
#include "vtkWeakPointer.h"
#include "vtkWidgetRepresentation.h"

#include <vtkstd/list>

vtkStandardNewMacro(vtkSMNewWidgetRepresentationProxy);

class vtkSMNewWidgetRepresentationObserver : public vtkCommand
{
public:
  static vtkSMNewWidgetRepresentationObserver *New() 
    { return new vtkSMNewWidgetRepresentationObserver; }
  virtual void Execute(vtkObject*, unsigned long event, void*)
    {
      if (this->Proxy)
        {
        this->Proxy->ExecuteEvent(event);
        }
    }
  vtkSMNewWidgetRepresentationObserver():Proxy(0) {}
  vtkSMNewWidgetRepresentationProxy* Proxy;
};

struct vtkSMNewWidgetRepresentationInternals
{
  typedef vtkstd::list<vtkSmartPointer<vtkSMLink> > LinksType;
  LinksType Links;
  vtkWeakPointer<vtkSMRenderViewProxy> ViewProxy;
};

//----------------------------------------------------------------------------
vtkSMNewWidgetRepresentationProxy::vtkSMNewWidgetRepresentationProxy()
{
  this->SetLocation(vtkProcessModule::CLIENT_AND_SERVERS);
  this->RepresentationProxy = 0;
  this->WidgetProxy = 0;
  this->Widget = 0;
  this->Observer = vtkSMNewWidgetRepresentationObserver::New();
  this->Observer->Proxy = this;
  this->Internal = new vtkSMNewWidgetRepresentationInternals;
}

//----------------------------------------------------------------------------
vtkSMNewWidgetRepresentationProxy::~vtkSMNewWidgetRepresentationProxy()
{
  this->RepresentationProxy = 0;
  this->WidgetProxy = 0;
  this->Widget = 0;
  this->Observer->Proxy = 0;
  this->Observer->Delete();

  if (this->Internal)
    {
    delete this->Internal;
    }
}

//-----------------------------------------------------------------------------
void vtkSMNewWidgetRepresentationProxy::CreateVTKObjects()
{
  if (this->ObjectsCreated)
    {
    return;
    }

  this->RepresentationProxy = this->GetSubProxy("Prop");
  if (!this->RepresentationProxy)
    {
    this->RepresentationProxy = this->GetSubProxy("Prop2D");
    }
  if (!this->RepresentationProxy)
    {
    vtkErrorMacro(
      "A representation proxy must be defined as a Prop (or Prop2D) sub-proxy");
    return;
    }
  this->RepresentationProxy->SetLocation(
    vtkProcessModule::RENDER_SERVER | vtkProcessModule::CLIENT);

  this->WidgetProxy = this->GetSubProxy("Widget");
  if (this->WidgetProxy)
    {
    this->WidgetProxy->SetLocation(vtkProcessModule::CLIENT);
    }

  this->Superclass::CreateVTKObjects();

  // Bind the Widget and the representations on the server side
  vtkSMMessage msg;
  msg.set_global_id(this->GlobalID);
  msg.set_location(vtkProcessModule::CLIENT | vtkProcessModule::RENDER_SERVER);
  VariantList *args = msg.MutableExtension(InvokeRequest::arguments);
  Variant* arg = args->add_variant();
  arg->set_type(Variant::PROXY);
  arg->add_proxy_global_id(this->RepresentationProxy->GetGlobalID());
  msg.SetExtension(InvokeRequest::method, "SetRepresentation");

  // Make the call
  this->Invoke(&msg);

  // Location 0 is for prototype objects !!! No need to send to the server something.
  if (!this->WidgetProxy || this->Location == 0)
    {
    return;
    }

  vtkSMProxyProperty* pp = vtkSMProxyProperty::SafeDownCast(
    this->WidgetProxy->GetProperty("Representation"));
  if (pp)
    {
    pp->AddProxy(this->RepresentationProxy);
    }
  this->WidgetProxy->UpdateVTKObjects();

  // Get the local VTK object for that widget
  this->Widget = vtkAbstractWidget::SafeDownCast(
    this->WidgetProxy->GetClientSideObject());

  if (this->Widget)
    {
    this->Widget->AddObserver(
      vtkCommand::StartInteractionEvent, this->Observer);
    this->Widget->AddObserver(
      vtkCommand::EndInteractionEvent, this->Observer);
    this->Widget->AddObserver(
      vtkCommand::InteractionEvent, this->Observer);
    }

  vtk3DWidgetRepresentation* clientObject =
    vtk3DWidgetRepresentation::SafeDownCast(this->GetClientSideObject());
  clientObject->SetWidget(this->Widget);

  // Since links copy values from input to output,
  // we need to make sure that input properties i.e. the info
  // properties are not empty.
  this->UpdatePropertyInformation();

  vtkSMPropertyIterator* piter = this->NewPropertyIterator();
  for(piter->Begin(); !piter->IsAtEnd(); piter->Next())
    {
    vtkSMProperty* prop = piter->GetProperty();
    vtkSMProperty* info = prop->GetInformationProperty();
    if (info)
      {      
      // This ensures that the property value from the loaded state is
      // preserved, and not overwritten by the default value from 
      // the property information
      info->Copy(prop);
      
      vtkSMPropertyLink* link = vtkSMPropertyLink::New();
      link->AddLinkedProperty(this, 
                              piter->GetKey(), 
                              vtkSMLink::OUTPUT);
      link->AddLinkedProperty(this, 
                              this->GetPropertyName(info),
                              vtkSMLink::INPUT);
      this->Internal->Links.push_back(link);
      link->Delete();
      }
    }
  piter->Delete();

}

//-----------------------------------------------------------------------------
void vtkSMNewWidgetRepresentationProxy::ExecuteEvent(unsigned long event)
{
  this->InvokeEvent(event);

  if (event == vtkCommand::StartInteractionEvent)
    {
    vtkPVGenericRenderWindowInteractor* inter =
      vtkPVGenericRenderWindowInteractor::SafeDownCast(
        this->Widget->GetInteractor());
    if (inter)
      {
      inter->InteractiveRenderEnabledOn();
      }
    vtkSMProperty* startInt = 
      this->RepresentationProxy->GetProperty("OnStartInteraction");
    if (startInt)
      {
      startInt->Modified();
      this->RepresentationProxy->UpdateProperty("OnStartInteraction");
      }
    }
  else if (event == vtkCommand::InteractionEvent)
    {
    this->RepresentationProxy->UpdatePropertyInformation();
    this->UpdateVTKObjects();

    vtkSMProperty* interaction = 
      this->RepresentationProxy->GetProperty("OnInteraction");
    if (interaction)
      {
      interaction->Modified();
      this->RepresentationProxy->UpdateProperty("OnInteraction");
      }
    }
  else if (event == vtkCommand::EndInteractionEvent)
    {
    vtkPVGenericRenderWindowInteractor* inter =
      vtkPVGenericRenderWindowInteractor::SafeDownCast(
        this->Widget->GetInteractor());
    if (inter)
      {
      inter->InteractiveRenderEnabledOff();
      }
    vtkSMProperty* sizeHandles = 
      this->RepresentationProxy->GetProperty("SizeHandles");
    if (sizeHandles)
      {
      sizeHandles->Modified();
      this->RepresentationProxy->UpdateProperty("SizeHandles");
      }
    vtkSMProperty* endInt = 
      this->RepresentationProxy->GetProperty("OnEndInteraction");
    if (endInt)
      {
      endInt->Modified();
      this->RepresentationProxy->UpdateProperty("OnEndInteraction");
      }
    }
}

//----------------------------------------------------------------------------
void vtkSMNewWidgetRepresentationProxy::UnRegister(vtkObjectBase* obj)
{
  vtkProcessModule* pm = vtkProcessModule::GetProcessModule();
  // If the object is not being deleted by the interpreter and it
  // has a reference count of 2 (SelfID and the reference that is
  // being released), delete the internals so that the links
  // release their references to the proxy
  if ( pm && this->Internal )
    {
    int size = this->Internal->Links.size();
    if (size > 0 && this->ReferenceCount == 2 + 2*size)
      {
      vtkSMNewWidgetRepresentationInternals* aInternal = this->Internal;
      this->Internal = 0;
      delete aInternal;
      aInternal = 0;
      }
    }

  this->Superclass::UnRegister(obj);
}

//----------------------------------------------------------------------------
void vtkSMNewWidgetRepresentationProxy::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
}


