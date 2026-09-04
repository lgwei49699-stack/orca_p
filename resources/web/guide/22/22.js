
var m_ProfileItem;

var FilamentPriority=new Array( "pla","abs","pet","tpu","pc");
var VendorPriority=new Array("generic");
  
function OnInit()
{
	TranslatePage();
	
	RequestProfile();

}

function RequestProfile()
{
	var tSend={};
	tSend['sequence_id']=Math.round(new Date() / 1000);
	tSend['command']="request_userguide_profile";
	
	SendWXMessage( JSON.stringify(tSend) );
}

function HandleStudio(pVal)
{
	let strCmd=pVal['command'];
	//alert(strCmd);
	
	if(strCmd=='response_userguide_profile')
	{
		m_ProfileItem=pVal['response'];
		SortUI();
	}
}

function GetFilamentShortname( sName )
{
	let sShort=sName.split('@')[0].trim();
	
	return sShort;
}

function SplitFilamentList( filamentList )
{
	let result=new Array();
	let items=(filamentList || '').split(';');
	for(let n=0;n<items.length;n++)
	{
		let item=items[n].trim();
		if(item!='' && result.indexOf(item)<0)
			result.push(item);
	}
	return result;
}

function IsFilamentFamilyTemplate( filamentName, modelList )
{
	return modelList=='' && /@.*-Series\s*$/i.test(filamentName);
}

function FindFilamentInput( vendor, filamentType, shortName )
{
	return $('#ItemBlockArea input').filter(function() {
		return $(this).attr('vendor')==vendor
			&& $(this).attr('filatype')==filamentType
			&& $(this).attr('name')==shortName;
	}).first();
}

function GetApplicableModelNames( modelList, selectedModels )
{
	let names=new Array();
	if(modelList=='')
		return names;

	for(let n=0;n<selectedModels.length;n++)
	{
		let model=selectedModels[n];
		let matches=false;
		let nozzles=model['nozzle_selected'].split(';');
		for(let m=0;m<nozzles.length && !matches;m++)
		{
			if(modelList.indexOf('['+model['model']+'++'+nozzles[m]+']')>=0)
				matches=true;
		}
		if(matches && names.indexOf(model['model'])<0)
			names.push(model['model']);
	}
	return names;
}

function FilamentInputSupportsModel( filamentInput, modelName )
{
	let modelNames=SplitFilamentList(filamentInput.getAttribute('displaymodels'));
	return modelNames.indexOf(modelName)>=0;
}

function EnsureDefaultsForUncoveredModels()
{
	let modelInputs=$('#MachineList input:gt(0):checked');
	let filamentInputs=$('#ItemBlockArea .MItem input');

	for(let n=0;n<modelInputs.length;n++)
	{
		let modelName=modelInputs[n].getAttribute('mode');
		let hasSelectedSpecific=false;
		for(let m=0;m<filamentInputs.length;m++)
		{
			// A row merged with a universal preset (for example Generic PLA)
			// must not suppress the printer's own recommended default.
			if(filamentInputs[m].checked
				&& filamentInputs[m].getAttribute('model')!=''
				&& FilamentInputSupportsModel(filamentInputs[m], modelName))
			{
				hasSelectedSpecific=true;
				break;
			}
		}
		if(hasSelectedSpecific)
			continue;

		let defaultMaterialNames={};
		for(let m=0;m<m_ProfileItem['model'].length;m++)
		{
			let model=m_ProfileItem['model'][m];
			if(model['model']!=modelName)
				continue;
			let names=SplitFilamentList(model['materials']);
			for(let p=0;p<names.length;p++)
				defaultMaterialNames[names[p]]=true;
		}

		for(let m=0;m<filamentInputs.length;m++)
		{
			let names=SplitFilamentList(filamentInputs[m].getAttribute('filalist'));
			for(let p=0;p<names.length;p++)
			{
				if(defaultMaterialNames[names[p]]===true)
				{
					filamentInputs[m].checked=true;
					break;
				}
			}
		}
	}
}

function GetSelectedPrinterVariants()
{
	let variants=new Array();
	for(let n=0;n<m_ProfileItem['model'].length;n++)
	{
		let model=m_ProfileItem['model'][n];
		let nozzles=(model['nozzle_selected'] || '').split(';');
		for(let m=0;m<nozzles.length;m++)
		{
			let nozzle=nozzles[m].trim();
			if(nozzle=='')
				continue;
			let variant='['+model['model']+'++'+nozzle+']';
			if(variants.indexOf(variant)<0)
				variants.push(variant);
		}
	}
	return variants;
}

function ResolveFilamentInputNames( filamentInput )
{
	let candidates=SplitFilamentList(filamentInput.getAttribute('filalist'));
	let selectedVariants=GetSelectedPrinterVariants();
	let specificCandidates=new Array();
	let universalNames=new Array();

	for(let n=0;n<candidates.length;n++)
	{
		let name=candidates[n];
		if(!m_ProfileItem['filament'].hasOwnProperty(name))
			continue;
		let models=m_ProfileItem['filament'][name]['models'] || '';
		if(models=='')
		{
			universalNames.push(name);
			continue;
		}

		let modelVariants=models.match(/\[[^\]]+\]/g) || [];
		specificCandidates.push({name: name, models: models, scopeSize: modelVariants.length});
	}

	if(selectedVariants.length==0)
		return universalNames;

	let resolvedNames=new Array();
	let needsUniversal=false;
	for(let n=0;n<selectedVariants.length;n++)
	{
		let variant=selectedVariants[n];
		let bestCandidate=null;
		for(let m=0;m<specificCandidates.length;m++)
		{
			let candidate=specificCandidates[m];
			if(candidate.models.indexOf(variant)<0)
				continue;
			if(bestCandidate==null || candidate.scopeSize<bestCandidate.scopeSize)
				bestCandidate=candidate;
		}
		if(bestCandidate==null)
			needsUniversal=true;
		else if(resolvedNames.indexOf(bestCandidate.name)<0)
			resolvedNames.push(bestCandidate.name);
	}
	return needsUniversal ? resolvedNames.concat(universalNames) : resolvedNames;
}

function UpdateFilamentLabel( filamentInput )
{
	filamentInput.siblings('.FilamentName').text(filamentInput.attr('name'));
}


function SortUI()
{
	var ModelList=new Array();
	
	let nMode=m_ProfileItem["model"].length;
	for(let n=0;n<nMode;n++)
	{
		let OneMode=m_ProfileItem["model"][n];
		
		if( OneMode["nozzle_selected"]!="" )
			ModelList.push(OneMode);
	}
	
	//model
	let HtmlMode='';
	nMode=ModelList.length;
	for(let n=0;n<nMode;n++)
	{
		let sModel=ModelList[n];	
		/* ORCA use label tag to allow checkbox to toggle when user ckicked to text */
		HtmlMode+='<label><input type="checkbox" mode="'+sModel['model']+'"  nozzle="'+sModel['nozzle_selected']+'"   onChange="MachineClick()" />'+sModel['model']+'</label>';
	}
	
	$('#MachineList .CValues').append(HtmlMode);	
	$('#MachineList .CValues input').prop("checked",true);
	if(nMode<=1)
	{
		$('#MachineList .CValues label:first').hide();
	}
	
	// Show printer-specific presets before universal presets. Keep Generic first
	// inside each scope, and append immediately so nozzle variants merge reliably.
	let ModelFilamentArray=new Array();
	let ModelGenericFilamentArray=new Array();
	let UniversalFilamentArray=new Array();
	let UniversalGenericFilamentArray=new Array();
	for( let key in m_ProfileItem['filament'] )
	{
		let OneFila=m_ProfileItem['filament'][key];
		let IsGeneric=OneFila['vendor'].toLowerCase() === 'generic';
		let IsUniversal=(OneFila['models'] || '')=='';
		if(IsUniversal && IsGeneric)
			UniversalGenericFilamentArray.push(OneFila);
		else if(IsUniversal)
			UniversalFilamentArray.push(OneFila);
		else if(IsGeneric)
			ModelGenericFilamentArray.push(OneFila);
		else
			ModelFilamentArray.push(OneFila);
	}
	let SortedFilamentArray=ModelGenericFilamentArray.concat(
		ModelFilamentArray,
		UniversalGenericFilamentArray,
		UniversalFilamentArray
	);
	var TypeHtmlArray={};
	var VendorHtmlArray={};
	for( let n=0;n<SortedFilamentArray.length;n++ )
	{
		let OneFila=SortedFilamentArray[n];
		
		//alert(JSON.stringify(OneFila));
		
		let fWholeName=OneFila['name'].trim();
		let fShortName=GetFilamentShortname( OneFila['name'] );
		let fVendor=OneFila['vendor'];
		let fType=OneFila['type'];
		let fSelect=OneFila['selected'];
		let fModel=OneFila['models'];

		// Entries named "@...-Series" are inheritance templates. Their concrete
		// nozzle presets are the selectable entries shown by this page.
		if(IsFilamentFamilyTemplate(fWholeName, fModel))
			continue;
		
		
        let bFind=false;		
		//let bCheck=$("#MachineList input:first").prop("checked");
		if( fModel=='')
		{
			// Orca: hide
			bFind=true;
		}
		else
		{
			//check in modellist		    
		    let nModelAll=ModelList.length;
		    for(let m=0;m<nModelAll;m++)
		    {
	    		let sOne=ModelList[m];
			
				let OneName=sOne['model'];
				let NozzleArray=sOne["nozzle_selected"].split(';');
				
				let nNozzle=NozzleArray.length;
				
				for( let b=0;b<nNozzle;b++ )
				{
					let nowModel= OneName+"++"+NozzleArray[b];
					if(fModel.indexOf(nowModel)>=0)
					{
						bFind=true;
						break;
					}
				}
			}
		}
		
		if(bFind)
		{
			let applicableModels=GetApplicableModelNames(fModel, ModelList);
			let modelScope=applicableModels.join(';');

			//Type
			let LowType=fType.toLowerCase();
		    if(!TypeHtmlArray.hasOwnProperty(LowType))
		    {
				/* ORCA use label tag to allow checkbox to toggle when user ckicked to text */
			    let HtmlType='<label><input type="checkbox" filatype="'+fType+'" onChange="FilaClick()"   />'+fType+'</label>';
			
				TypeHtmlArray[LowType]=HtmlType;
		    }
			
			//Vendor
			let lowVendor=fVendor.toLowerCase();
			if(!VendorHtmlArray.hasOwnProperty(lowVendor))
		    {
				/* ORCA use label tag to allow checkbox to toggle when user ckicked to text */
			    let HtmlVendor='<label><input type="checkbox" vendor="'+fVendor+'"  onChange="VendorClick()" />'+fVendor+'</label>';
				
				VendorHtmlArray[lowVendor]=HtmlVendor;
		    }
			
			//Filament
			let pFila=FindFilamentInput(fVendor, fType, fShortName);
			// Match the vendor slicer UI: printer compatibility is a filter, not
			// part of the visible material name. Exact presets remain in filalist.
	        if(pFila.length==0)
		    {
				/* ORCA use label tag to allow checkbox to toggle when user ckicked to text */
				let item=$('<label>').addClass('MItem');
				pFila=$('<input>').attr({
					type: 'checkbox',
					vendor: fVendor,
					filatype: fType,
					filalist: fWholeName+';',
					model: fModel,
					name: fShortName,
					displaymodels: modelScope
				});
				item.append(pFila);
				item.append($('<span>').addClass('FilamentName'));
				$('#ItemBlockArea').append(item);
		    } 
			else
			{
				let strModel=pFila.attr("model");
				let strFilalist=pFila.attr("filalist");
				let displayModels=SplitFilamentList(pFila.attr('displaymodels'));
				
				if(strModel == '' || fModel == '')
					pFila.attr("model", '');
				else
					pFila.attr("model", strModel+fModel);
				for(let m=0;m<applicableModels.length;m++)
				{
					if(displayModels.indexOf(applicableModels[m])<0)
						displayModels.push(applicableModels[m]);
				}
				pFila.attr('displaymodels', displayModels.join(';'));
					
				let filamentNames=SplitFilamentList(strFilalist);
				if(filamentNames.indexOf(fWholeName)<0)
					filamentNames.push(fWholeName);
				pFila.attr("filalist", filamentNames.join(';')+';');
			}

			UpdateFilamentLabel(pFila);
			
			if(fSelect*1==1)
			{
				//alert( fWholeName+' - '+fShortName+' - '+fVendor+' - '+fType+' - '+fSelect+' - '+fModel );
					
				pFila.prop("checked",true);
			}
//			else
//				$("#ItemBlockArea input[vendor='"+fVendor+"'][model='"+fModel+"'][filatype='"+fType+"'][name='"+key+"']").prop("checked",false);			
		}
	}

	//Sort TypeArray
	let TypeAdvNum=FilamentPriority.length;
	for( let n=0;n<TypeAdvNum;n++ )
	{
		let strType=FilamentPriority[n];
		
		if( TypeHtmlArray.hasOwnProperty( strType ) )
		{
			$("#FilatypeList .CValues").append( TypeHtmlArray[strType] );
			delete( TypeHtmlArray[strType] );
		}
	}
    for(let key in TypeHtmlArray )
	{
		$("#FilatypeList .CValues").append( TypeHtmlArray[key] );
	}
	$("#FilatypeList .CValues input").prop("checked",true);
	
	//Sort VendorArray
	let VendorAdvNum=VendorPriority.length;
	for( let n=0;n<VendorAdvNum;n++ )
	{
		let strVendor=VendorPriority[n];
		
		if( VendorHtmlArray.hasOwnProperty( strVendor ) )
		{
			$("#VendorList .CValues").append( VendorHtmlArray[strVendor] );
			delete( VendorHtmlArray[strVendor] );
		}
	}
    for(let key in VendorHtmlArray )
	{
		$("#VendorList .CValues").append( VendorHtmlArray[key] );
	}	
	$("#VendorList .CValues input").prop("checked",true);
	
	//------
	EnsureDefaultsForUncoveredModels();
}


function ChooseAllMachine()
{
	let bCheck=$("#MachineList input:first").prop("checked");
	
	$("#MachineList input").prop("checked",bCheck);
	
	SortFilament();
}

function MachineClick()
{
	let nChecked=$("#MachineList input:gt(0):checked").length
	let nAll    =$("#MachineList input:gt(0)").length
	
	if(nAll==nChecked)
	{
		$("#MachineList input:first").prop("checked",true);
	}
	else
	{
		$("#MachineList input:first").prop("checked",false);
	}
	
	SortFilament();
}

function ChooseAllFilament()
{
	let bCheck=$("#FilatypeList input:first").prop("checked");	
	$("#FilatypeList input").prop("checked",bCheck);	
	
	SortFilament();
}

function FilaClick()
{
	let nChecked=$("#FilatypeList input:gt(0):checked").length
	let nAll    =$("#FilatypeList input:gt(0)").length
	
	if(nAll==nChecked)
	{
		$("#FilatypeList input:first").prop("checked",true);
	}
	else
	{
		$("#FilatypeList input:first").prop("checked",false);
	}
	
	SortFilament();	
}

function ChooseAllVendor()
{
	let bCheck=$("#VendorList input:first").prop("checked");	
	$("#VendorList input").prop("checked",bCheck);	
	
	SortFilament();
}

function VendorClick()
{
	let nChecked=$("#VendorList input:gt(0):checked").length
	let nAll    =$("#VendorList input:gt(0)").length
	
	if(nAll==nChecked)
	{
		$("#VendorList input:first").prop("checked",true);
	}
	else
	{
		$("#VendorList input:first").prop("checked",false);
	}
	
	SortFilament();
}



function SortFilament()
{
	let FilaNodes=$("#ItemBlockArea .MItem");
	let nFilament=FilaNodes.length;
	//$("#ItemBlockArea .MItem").hide();
	
	//ModelList
	let pModel=$("#MachineList input:checked");
	let nModel=pModel.length;
	let ModelList=new Array();
	for(let n=0;n<nModel;n++)
	{
		let OneModel=pModel[n];
		
		let mName=OneModel.getAttribute("mode");
		if( mName=='all' )
		{
			continue;
		}
		else
		{
			let mNozzle=OneModel.getAttribute("nozzle");
			let NozzleArray=mNozzle.split(';');
			
			for( let bb=0;bb<NozzleArray.length;bb++ )
			{
				let NewModel='['+mName+'++'+NozzleArray[bb]+']';
			
				ModelList.push( NewModel );
			}
		}
	}
	
	//TypeList
	let pType=$("#FilatypeList input:gt(0):checked");
	let nType=pType.length;
	let TypeList=new Array();
	for(let n=0;n<nType;n++)
	{
		let OneType=pType[n];
		TypeList.push(  OneType.getAttribute("filatype") );
	}	
	
	//VendorList
	let pVendor=$("#VendorList input:gt(0):checked");
	let nVendor=pVendor.length;
	let VendorList=new Array();
	for(let n=0;n<nVendor;n++)
	{
		let OneVendor=pVendor[n];
		VendorList.push(  OneVendor.getAttribute("vendor") );
	}		
	
	
	//Update Filament UI
	for(let m=0;m<nFilament;m++)
	{
		let OneNode=FilaNodes[m];
		let OneFF=OneNode.getElementsByTagName("input")[0];
		
	    let fModel=OneFF.getAttribute("model");
		let fVendor=OneFF.getAttribute("vendor");
		let fType=OneFF.getAttribute("filatype");
		let fName=OneFF.getAttribute("name");
		
		if(TypeList.in_array(fType) && VendorList.in_array(fVendor))
		{
			let HasModel=false;
			for(let m=0;m<ModelList.length;m++)
			{
				let ModelSrc=ModelList[m];
				
				if( fModel.indexOf(ModelSrc)>=0)
				{
					HasModel=true;
					break;
				}
			}
			
			if(HasModel || fModel=='')
			    $(OneNode).show();
			else
				$(OneNode).hide();
		}
		else
			$(OneNode).hide();
	}
}

function ChooseDefaultFilament()
{
	//ModelList
	let pModel=$("#MachineList input:gt(0):checked");
	let nModel=pModel.length;
	let ModelList=new Array();
	for(let n=0;n<nModel;n++)
	{
		let OneModel=pModel[n];
		ModelList.push(  OneModel.getAttribute("mode") );
	}	
	
	//DefaultMaterialList
	let DefaultMaterialNames={};
	let nMode=m_ProfileItem["model"].length;
	for(let n=0;n<nMode;n++)
	{
		let OneMode=m_ProfileItem["model"][n];
		let ModeName=OneMode['model'];
		
		if( ModelList.indexOf(ModeName)>-1 )	
		{
			let names=SplitFilamentList(OneMode['materials']);
			for(let m=0;m<names.length;m++)
				DefaultMaterialNames[names[m]]=true;
		}
	}	
	
	//Filament
	let FilaNodes=$("#ItemBlockArea .MItem");
    let nFilament=FilaNodes.length;
    for(let m=0;m<nFilament;m++)
	{
		let OneNode=FilaNodes[m];
		let OneFF=OneNode.getElementsByTagName("input")[0];
		$(OneFF).prop("checked",false);
		
		let filamentArray=SplitFilamentList(OneFF.getAttribute("filalist"));
		
		let HasModel=false;
		let NowFilaLength=filamentArray.length;
		for(let p=0;p<NowFilaLength;p++)
		{
			let NowFila=filamentArray[p];
		
			if( DefaultMaterialNames[NowFila]===true )
			{
				HasModel=true;
				break;
			}
		}
			
		if(HasModel)
		    $(OneFF).prop("checked",true);
	}
	
	ShowNotice(0);
}

function SelectAllFilament( nShow )
{
	if( nShow==0 )
	{
		$('#ItemBlockArea input').prop("checked",false);
	}
	else
	{
		$('#ItemBlockArea .MItem:visible input').prop("checked",true);
	}
}

function ShowNotice( nShow )
{
	if(nShow==0)
	{
		$("#NoticeMask").hide();
		$("#NoticeBody").hide();
	}
	else
	{
		$("#NoticeMask").show();
		$("#NoticeBody").show();
	}
}


function ResponseFilamentResult()
{
	let FilaSelectedList= $("#ItemBlockArea input:checked");
	let nAll=FilaSelectedList.length;

	if( nAll==0 )
	{
		ShowNotice(1);
		return false;
	}
	
	let FilaArray=new Array();
	let AddedFilaments={};
	for(let n=0;n<nAll;n++)
	{
		let filamentNames=ResolveFilamentInputNames(FilaSelectedList[n]);
		for(let m=0;m<filamentNames.length;m++)
		{
			let name=filamentNames[m];
			if(m_ProfileItem['filament'].hasOwnProperty(name) && !AddedFilaments[name])
			{
				FilaArray.push(name);
				AddedFilaments[name]=true;
			}
		}
	}
	
	var tSend={};
	tSend['sequence_id']=Math.round(new Date() / 1000);
	tSend['command']="save_userguide_filaments";
	tSend['data']={};
	tSend['data']['filament']=FilaArray;
		
	SendWXMessage( JSON.stringify(tSend) );
	
	return true;
}


function ReturnPreviewPage()
{
	let nMode=m_ProfileItem["model"].length;
	
	if( nMode==1)
		document.location.href="../1/index.html";
	else
		document.location.href="../21/index.html";	
}


function FinishGuide()
{
	let bRet=ResponseFilamentResult();
	
	if(bRet)	
	{
		var tSend={};
		tSend['sequence_id']=Math.round(new Date() / 1000);
		tSend['command']="user_guide_finish";
		tSend['data']={};
		tSend['data']['action']="finish";
		
		SendWXMessage( JSON.stringify(tSend) );	
	}
	//window.location.href="../6/index.html";
}
