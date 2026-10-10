#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "ToolMenus.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "DesktopPlatformModule.h"
#include "IDesktopPlatform.h"
#include "Factories/FontFactory.h"
#include "Engine/Font.h"
#include "Engine/FontFace.h"
#include "Fonts/CompositeFont.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/MessageDialog.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

#include "Framework/Application/SlateApplication.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SWindow.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "ToolMenuMisc.h"

DEFINE_LOG_CATEGORY_STATIC(LogFontImporter, Log, All);

namespace FontImporter
{
    struct FStyleRule
    {
        const TCHAR* Suffix;
        const TCHAR* DisplayName;
        int32 SortIndex;
    };

    // Ordering matters: longest/specific matches must precede short ones.
    static const FStyleRule StyleRules[] =
    {
        { TEXT("ExtraBoldItalic"),  TEXT("ExtraBold Italic"), 15 },
        { TEXT("UltraBoldItalic"),  TEXT("ExtraBold Italic"), 15 },
        { TEXT("ExtraLightItalic"), TEXT("ExtraLight Italic"), 3 },
        { TEXT("UltraLightItalic"), TEXT("ExtraLight Italic"), 3 },
        { TEXT("SemiBoldItalic"),   TEXT("SemiBold Italic"), 11 },
        { TEXT("DemiBoldItalic"),   TEXT("SemiBold Italic"), 11 },
        { TEXT("BlackItalic"),      TEXT("Black Italic"), 17 },
        { TEXT("HeavyItalic"),      TEXT("Black Italic"), 17 },
        { TEXT("MediumItalic"),     TEXT("Medium Italic"), 9 },
        { TEXT("RegularItalic"),    TEXT("Italic"), 7 },
        { TEXT("NormalItalic"),     TEXT("Italic"), 7 },
        { TEXT("LightItalic"),      TEXT("Light Italic"), 5 },
        { TEXT("BoldItalic"),       TEXT("Bold Italic"), 13 },
        { TEXT("ThinItalic"),       TEXT("Thin Italic"), 1 },
        { TEXT("ExtraBold"),        TEXT("ExtraBold"), 14 },
        { TEXT("UltraBold"),        TEXT("ExtraBold"), 14 },
        { TEXT("ExtraLight"),       TEXT("ExtraLight"), 2 },
        { TEXT("UltraLight"),       TEXT("ExtraLight"), 2 },
        { TEXT("SemiBold"),         TEXT("SemiBold"), 10 },
        { TEXT("DemiBold"),         TEXT("SemiBold"), 10 },
        { TEXT("Regular"),          TEXT("Regular"), 6 },
        { TEXT("Normal"),           TEXT("Regular"), 6 },
        { TEXT("Medium"),           TEXT("Medium"), 8 },
        { TEXT("Italic"),           TEXT("Italic"), 7 },
        { TEXT("Oblique"),          TEXT("Italic"), 7 },
        { TEXT("Black"),            TEXT("Black"), 16 },
        { TEXT("Heavy"),            TEXT("Black"), 16 },
        { TEXT("Light"),            TEXT("Light"), 4 },
        { TEXT("Roman"),            TEXT("Regular"), 6 },
        { TEXT("Book"),             TEXT("Regular"), 6 },
        { TEXT("Bold"),             TEXT("Bold"), 12 },
        { TEXT("Thin"),             TEXT("Thin"), 0 }
    };

    struct FSourceFace
    {
        FString FilePath;
        FString Family;
        FString FamilyId;
        FString Style;
        FString StyleId;
        int32 SortIndex = 6;
    };

    static bool IsSeparator(const TCHAR Char)
    {
        return Char == TEXT('-') || Char == TEXT('_') || Char == TEXT(' ') || Char == TEXT('.');
    }

    static FString StripSeparators(const FString& Input)
    {
        FString Result;
        for (const TCHAR Char : Input)
        {
            if (!IsSeparator(Char))
            {
                Result.AppendChar(Char);
            }
        }
        return Result;
    }

    static FString SafeAssetName(const FString& Input)
    {
        FString Result;
        bool bPreviousUnderscore = false;
        for (const TCHAR Char : Input)
        {
            if (FChar::IsAlnum(Char))
            {
                Result.AppendChar(Char);
                bPreviousUnderscore = false;
            }
            else if (!bPreviousUnderscore && !Result.IsEmpty())
            {
                Result.AppendChar(TEXT('_'));
                bPreviousUnderscore = true;
            }
        }
        while (Result.EndsWith(TEXT("_")))
        {
            Result.LeftChopInline(1);
        }
        return Result;
    }

    static void TrimSeparators(FString& String)
    {
        while (!String.IsEmpty() && IsSeparator(String[0]))
        {
            String.RightChopInline(1);
        }
        while (!String.IsEmpty() && IsSeparator(String[String.Len() - 1]))
        {
            String.LeftChopInline(1);
        }
    }

    static FSourceFace DetectFace(const FString& FilePath)
    {
        FSourceFace Out;
        Out.FilePath = FilePath;

        FString Stem = FPaths::GetBaseFilename(FilePath);
        if (Stem.StartsWith(TEXT("FF_"), ESearchCase::IgnoreCase))
        {
            Stem.RightChopInline(3);
        }
        else if (Stem.StartsWith(TEXT("FontFace_"), ESearchCase::IgnoreCase))
        {
            Stem.RightChopInline(9);
        }

        const FString Compact = StripSeparators(Stem);
        Out.Style = TEXT("Regular");
        Out.SortIndex = 6;

        for (const FStyleRule& Rule : StyleRules)
        {
            const FString Suffix(Rule.Suffix);
            if (Compact.Len() <= Suffix.Len() || !Compact.EndsWith(Suffix, ESearchCase::IgnoreCase))
            {
                continue;
            }

            // Remove suffix characters from the original stem while allowing
            // separators in e.g. "Open Sans-Semi-Bold-Italic".
            int32 LettersToRemove = Suffix.Len();
            int32 CutAt = Stem.Len();
            while (CutAt > 0 && LettersToRemove > 0)
            {
                --CutAt;
                if (!IsSeparator(Stem[CutAt]))
                {
                    --LettersToRemove;
                }
            }
            if (LettersToRemove == 0)
            {
                Stem = Stem.Left(CutAt);
                Out.Style = Rule.DisplayName;
                Out.SortIndex = Rule.SortIndex;
            }
            break;
        }

        TrimSeparators(Stem);
        Out.Family = Stem;
        Out.FamilyId = SafeAssetName(Stem);
        Out.StyleId = Out.Style.Replace(TEXT(" "), TEXT(""));
        return Out;
    }

    static bool SaveAsset(UObject* Asset)
    {
        if (!IsValid(Asset))
        {
            return false;
        }

        UPackage* Package = Asset->GetOutermost();
        const FString Filename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);

        FSavePackageArgs SaveArgs;
        SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
        return UPackage::SavePackage(Package, Asset, *Filename, SaveArgs);
    }

    static UFontFace* CreateOrUpdateFace(
        const FSourceFace& Source, const FString& Destination,
        bool bUpdateExisting, bool& bWasChanged, FString& OutError)
    {
        bWasChanged = false;
        const FString AssetName = TEXT("FF_") + Source.FamilyId + TEXT("_") + Source.StyleId;
        const FString PackagePath = Destination / AssetName;
        const FString ObjectPath = PackagePath + TEXT(".") + AssetName;

        UObject* Existing = LoadObject<UObject>(nullptr, *ObjectPath);
        UFontFace* Face = Cast<UFontFace>(Existing);
        if (Existing && !Face)
        {
            OutError = FString::Printf(TEXT("Name collision: %s"), *ObjectPath);
            return nullptr;
        }

        if (Face && !bUpdateExisting)
        {
            return Face;
        }

        TArray<uint8> Bytes;
        if (!FFileHelper::LoadFileToArray(Bytes, *Source.FilePath) || Bytes.IsEmpty())
        {
            OutError = FString::Printf(TEXT("Unable to read: %s"), *Source.FilePath);
            return nullptr;
        }

        // Basic TTF/OTF signature validation, does not guarantee font integrity.
        if (Bytes.Num() < 12)
        {
            OutError = FString::Printf(TEXT("File too small: %s"), *Source.FilePath);
            return nullptr;
        }
        const bool bTrueType = Bytes[0] == 0 && Bytes[1] == 1 && Bytes[2] == 0 && Bytes[3] == 0;
        const bool bOpenType = Bytes[0] == 'O' && Bytes[1] == 'T' && Bytes[2] == 'T' && Bytes[3] == 'O';
        if (!bTrueType && !bOpenType)
        {
            OutError = FString::Printf(TEXT("Unsupported TTF/OTF header: %s"), *Source.FilePath);
            return nullptr;
        }

        const bool bNew = !Face;
        if (bNew)
        {
            UPackage* Package = CreatePackage(*PackagePath);
            Face = NewObject<UFontFace>(Package, FName(*AssetName), RF_Public | RF_Standalone | RF_Transactional);
        }
        else
        {
            Face->Modify();
        }

        // Inline embeds the data in the asset and is safe for packaging.
        Face->LoadingPolicy = EFontLoadingPolicy::Inline;
        Face->SourceFilename = Source.FilePath;
        Face->FontFaceData = FFontFaceData::MakeFontFaceData(MoveTemp(Bytes));
        Face->CacheSubFaces();
        Face->MarkPackageDirty();
        Face->PostEditChange();

        if (bNew)
        {
            FAssetRegistryModule::AssetCreated(Face);
        }

        bWasChanged = true;
        return Face;
    }

    static FString ImportDirectory(const FString& InDirectory, const FString& InDestination, bool bUpdateExisting)
    {
        FString RequestedDirectory = InDirectory;
        RequestedDirectory.TrimStartAndEndInline();
        if (RequestedDirectory.IsEmpty())
        {
            return TEXT("Choose a source directory first.");
        }
        FString Directory = FPaths::ConvertRelativePathToFull(RequestedDirectory);
        FString Destination = InDestination;
        Destination.TrimStartAndEndInline();
        while (Destination.EndsWith(TEXT("/")))
        {
            Destination.LeftChopInline(1);
        }

        if (!IFileManager::Get().DirectoryExists(*Directory))
        {
            return TEXT("Source directory does not exist.");
        }
        if (!Destination.StartsWith(TEXT("/Game/")) && Destination != TEXT("/Game"))
        {
            return TEXT("Select a destination inside /Game.");
        }
        if (!FPackageName::IsValidLongPackageName(Destination))
        {
            return TEXT("Invalid Unreal destination path.");
        }

        TArray<FString> Files;

        IFileManager::Get().FindFilesRecursive(
            Files, *Directory, TEXT("*.ttf"), true, false
        );

        IFileManager::Get().FindFilesRecursive(
            Files, *Directory, TEXT("*.otf"), true, false, false
        );
        
        Files.Sort();
        if (Files.IsEmpty())
        {
            return TEXT("No .ttf or .otf files found in the selected directory (or subdirectories).");
        }

        TMap<FString, TArray<FSourceFace>> Families;
        for (const FString& File : Files)
        {
            FSourceFace Source = DetectFace(File);
            if (!Source.FamilyId.IsEmpty())
            {
                Families.FindOrAdd(Source.FamilyId.ToLower()).Add(MoveTemp(Source));
            }
        }

        int32 ImportedFaces = 0;
        int32 ReusedFaces = 0;
        int32 CreatedFonts = 0;
        int32 UpdatedFonts = 0;
        int32 Errors = 0;
        TArray<FString> Warnings;
        TArray<UObject*> ChangedAssets;

        IAssetTools& AssetTools = FAssetToolsModule::GetModule().Get();
        TArray<FString> Keys;
        Families.GetKeys(Keys);
        Keys.Sort();

        for (const FString& FamilyKey : Keys)
        {
            TArray<FSourceFace>& Sources = Families.FindChecked(FamilyKey);
            Sources.Sort([](const FSourceFace& L, const FSourceFace& R)
            {
                return L.SortIndex == R.SortIndex ? L.FilePath < R.FilePath : L.SortIndex < R.SortIndex;
            });

            const FString FamilyId = Sources[0].FamilyId;
            const FString FontName = TEXT("F_") + FamilyId;
            const FString FontObjectPath = Destination / FontName + TEXT(".") + FontName;
            UObject* ExistingFontAsset = LoadObject<UObject>(nullptr, *FontObjectPath);
            UFont* Font = Cast<UFont>(ExistingFontAsset);
            if (ExistingFontAsset && !Font)
            {
                ++Errors;
                Warnings.Add(TEXT("Font name collision: ") + FontObjectPath);
                continue;
            }

            const bool bNewFont = !Font;
            if (!Font)
            {
                UFontFactory* Factory = NewObject<UFontFactory>();
                Font = Cast<UFont>(AssetTools.CreateAsset(FontName, Destination, UFont::StaticClass(), Factory));
                if (!Font)
                {
                    ++Errors;
                    Warnings.Add(TEXT("Failed to create ") + FontName);
                    continue;
                }
            }

            Font->Modify();
            Font->FontCacheType = EFontCacheType::Runtime;
            if (bNewFont)
            {
                Font->CompositeFont.DefaultTypeface.Fonts.Reset();
            }

            TSet<FString> SeenStyles;
            bool bFontChanged = bNewFont;
            for (const FSourceFace& Source : Sources)
            {
                if (SeenStyles.Contains(Source.StyleId))
                {
                    Warnings.Add(TEXT("Duplicate style skipped: ") + Source.FilePath);
                    continue;
                }
                SeenStyles.Add(Source.StyleId);

                bool bFaceChanged = false;
                FString Error;
                UFontFace* Face = CreateOrUpdateFace(Source, Destination, bUpdateExisting, bFaceChanged, Error);
                if (!Face)
                {
                    ++Errors;
                    Warnings.Add(Error);
                    continue;
                }
                if (bFaceChanged)
                {
                    ChangedAssets.AddUnique(Face);
                    ++ImportedFaces;
                }
                else
                {
                    ++ReusedFaces;
                }

                FTypefaceEntry* ExistingEntry = Font->CompositeFont.DefaultTypeface.Fonts.FindByPredicate(
                    [&Source](const FTypefaceEntry& Entry)
                    {
                        return Entry.Name == FName(*Source.Style);
                    });

                if (!ExistingEntry)
                {
                    FTypefaceEntry Entry(FName(*Source.Style));
                    Entry.Font = FFontData(Face, 0);
                    Font->CompositeFont.DefaultTypeface.Fonts.Add(MoveTemp(Entry));
                    bFontChanged = true;
                }
                else if (bUpdateExisting || bNewFont)
                {
                    ExistingEntry->Font = FFontData(Face, 0);
                    bFontChanged = true;
                }
            }

            if (bFontChanged)
            {
                Font->CompositeFont.MakeDirty();
                Font->MarkPackageDirty();
                Font->PostEditChange();
                ChangedAssets.AddUnique(Font);
                if (bNewFont) { ++CreatedFonts; }
                else { ++UpdatedFonts; }
            }
        }

        int32 Saved = 0;
        for (UObject* Asset : ChangedAssets)
        {
            if (SaveAsset(Asset))
            {
                ++Saved;
            }
            else
            {
                ++Errors;
                Warnings.Add(TEXT("Could not save: ") + Asset->GetPathName());
            }
        }

        FString Report = FString::Printf(
            TEXT("Found %d file(s) in %d family/families.\n")
            TEXT("Created/updated Font Faces: %d\n")
            TEXT("Existing Font Faces reused: %d\n")
            TEXT("Composite Fonts created: %d\n")
            TEXT("Composite Fonts updated: %d\n")
            TEXT("Assets saved: %d\n")
            TEXT("Errors: %d"),
            Files.Num(), Families.Num(), ImportedFaces, ReusedFaces,
            CreatedFonts, UpdatedFonts, Saved, Errors);
        if (!Warnings.IsEmpty())
        {
            Report += TEXT("\n\nNotes (first 15):\n");
            for (int32 Index = 0; Index < FMath::Min(15, Warnings.Num()); ++Index)
            {
                Report += Warnings[Index] + TEXT("\n");
            }
        }
        UE_LOG(LogFontImporter, Display, TEXT("%s"), *Report);
        return Report;
    }
}

class SFontImportWindow final : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SFontImportWindow) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs)
    {
        Destination = TEXT("/Game");
        FPathPickerConfig PickerConfig;
        PickerConfig.DefaultPath = Destination;
        PickerConfig.bAllowClassesFolder = false;
        PickerConfig.OnPathSelected = FOnPathSelected::CreateSP(this, &SFontImportWindow::OnPathSelected);

        FContentBrowserModule& Browser = FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
        TSharedRef<SWidget> PathPicker = Browser.Get().CreatePathPicker(PickerConfig);

        ChildSlot
        [
            SNew(SBorder).Padding(14)
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
                [ SNew(STextBlock).Text(FText::FromString(TEXT("Source directory (TTF / OTF, includes subfolders)"))) ]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().FillWidth(1.0f).Padding(0, 0, 8, 0)
                    [
                        SAssignNew(SourceText, SEditableTextBox)
                        .HintText(FText::FromString(TEXT("C:/Fonts/Roboto")))
                    ]
                    + SHorizontalBox::Slot().AutoWidth()
                    [ SNew(SButton).Text(FText::FromString(TEXT("Browse..."))).OnClicked(this, &SFontImportWindow::OnBrowse) ]
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)
                [ SNew(STextBlock).Text(FText::FromString(TEXT("Unreal destination: select a folder below"))) ]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
                [ SNew(STextBlock).Text(this, &SFontImportWindow::GetDestinationText) ]
                + SVerticalBox::Slot().FillHeight(1.0f).Padding(0, 0, 0, 10)
                [ PathPicker ]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
                [
                    SAssignNew(UpdateExisting, SCheckBox)
                    .IsChecked(ECheckBoxState::Unchecked)
                    [ SNew(STextBlock).Text(FText::FromString(TEXT("Refresh existing Font Face data and composite entries"))) ]
                ]
                + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)
                [ SNew(SButton).Text(FText::FromString(TEXT("Import Font Families"))).OnClicked(this, &SFontImportWindow::OnImport) ]
            ]
        ];
    }

private:

    TSharedPtr<SEditableTextBox> SourceText;
    TSharedPtr<SCheckBox> UpdateExisting;
    FString Destination;

    void OnPathSelected(const FString& Path)
    {
        Destination = Path;
    }

    FText GetDestinationText() const
    {
        return FText::FromString(Destination);
    }

    FReply OnBrowse()
    {
        IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
        if (DesktopPlatform)
        {
            FString Folder;
            const void* ParentHandle = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);
            if (DesktopPlatform->OpenDirectoryDialog(
                ParentHandle, TEXT("Choose a font directory"),
                SourceText->GetText().ToString(), Folder))
            {
                SourceText->SetText(FText::FromString(Folder));
            }
        }
        return FReply::Handled();
    }

    FReply OnImport()
    {
        const FString Report = FontImporter::ImportDirectory(
            SourceText->GetText().ToString(),
            Destination,
            UpdateExisting->IsChecked());
        FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Report));
        return FReply::Handled();
    }
};

class FFontImporterModule final : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
        UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FFontImporterModule::RegisterMenus));
    }

    virtual void ShutdownModule() override
    {
        UToolMenus::UnRegisterStartupCallback(this);
        UToolMenus::UnregisterOwner(this);
    }

private:
    void RegisterMenus()
    {
        FToolMenuOwnerScoped OwnerScoped(this);
        UToolMenu* Menu = UToolMenus::Get()->ExtendMenu(
            TEXT("MainFrame.MainMenu.Tools")
        );
        FToolMenuSection& Section = Menu->FindOrAddSection(
            TEXT("FontTools"),
            FText::FromString(TEXT("Font Tools")),
            FToolMenuInsert(NAME_None, EToolMenuInsertType::First)
        );
        Section.AddMenuEntry(
            TEXT("FontImporterOpen"),
            FText::FromString(TEXT("Import Font Family")),
            FText::FromString(TEXT("Create composite font families from a directory of TTF/OTF files.")),
            FSlateIcon(),
            FUIAction(FExecuteAction::CreateRaw(this, &FFontImporterModule::OpenImporter)));
    }

    void OpenImporter()
    {
        TSharedRef<SWindow> Window = SNew(SWindow)
            .Title(FText::FromString(TEXT("Import Font Family")))
            .ClientSize(FVector2D(610, 570))
            .SupportsMinimize(false)
            .SupportsMaximize(false);
        Window->SetContent(SNew(SFontImportWindow));
        FSlateApplication::Get().AddWindow(Window);
    }
};

IMPLEMENT_MODULE(FFontImporterModule, FontImporter)
