#include "functionseditor.h"
#include "ui_functionseditor.h"
#include "unused.h"
#include "utils.h"
#include "uiutils.h"
#include "functionseditormodel.h"
#include "pluginmanager.h"
#include "sqlfunctionplugin.h"
#include "dbtree/dbtree.h"
#include "dbtree/dbtreemodel.h"
#include "dbtree/dbtreeitem.h"
#include "iconmanager.h"
#include "syntaxhighlighterplugin.h"
#include "sqlitesyntaxhighlighter.h"
#include "common/userinputfilter.h"
#include <QDebug>

FunctionsEditor::FunctionsEditor(QWidget *parent) :
    MdiChild(parent),
    ui(new Ui::FunctionsEditor)
{
    init();
}

FunctionsEditor::~FunctionsEditor()
{
    delete ui;
}

bool FunctionsEditor::restoreSessionNextTime()
{
    return false;
}

bool FunctionsEditor::restoreSession(const QVariant &sessionValue)
{
    UNUSED(sessionValue);
    return true;
}

QString FunctionsEditor::getIconNameForMdiWindow()
{
    return "function";
}

QString FunctionsEditor::getTitleForMdiWindow()
{
    return tr("SQL function editor");
}

void FunctionsEditor::createActions()
{
    createAction(COMMIT, "commit", tr("Commit all function changes"), this, SLOT(commit()), ui->toolBar);
    createAction(ROLLBACK, "rollback", tr("Rollback all function changes"), this, SLOT(rollback()), ui->toolBar);
    ui->toolBar->addSeparator();
    createAction(ADD, "new_function", tr("Create new function"), this, SLOT(newFunction()), ui->toolBar);
    createAction(DELETE, "delete_function", tr("Delete selected function"), this, SLOT(deleteFunction()), ui->toolBar);

    // Args toolbar
    createAction(ARG_ADD, "insert_fn_arg", tr("Add function argument"), this, SLOT(addFunctionArg()), ui->argsToolBar);
    createAction(ARG_EDIT, "rename_fn_arg", tr("Rename function argument"), this, SLOT(editFunctionArg()), ui->argsToolBar);
    createAction(ARG_DEL, "delete_fn_arg", tr("Delete function argument"), this, SLOT(delFunctionArg()), ui->argsToolBar);
    ui->toolBar->addSeparator();
    createAction(ARG_MOVE_UP, "move_up", tr("Move function argument up"), this, SLOT(moveFunctionArgUp()), ui->argsToolBar);
    createAction(ARG_MOVE_DOWN, "move_down", tr("Move function argument down"), this, SLOT(moveFunctionArgDown()), ui->argsToolBar);
}

void FunctionsEditor::setupDefShortcuts()
{
}

void FunctionsEditor::init()
{
    ui->setupUi(this);
    clearEdits();
    ui->finalCodeGroup->setVisible(false);

    model = new FunctionsEditorModel(this);
    functionFilterModel = new QSortFilterProxyModel(this);
    functionFilterModel->setSourceModel(model);
    ui->list->setModel(functionFilterModel);

    dbListModel = new DbModel(this);
    dbListModel->setSourceModel(DBTREE->getModel());
    ui->databasesList->setModel(dbListModel);
    ui->databasesList->expandAll();

    ui->typeCombo->addItem(tr("Scalar"), FunctionManager::Function::SCALAR);
    ui->typeCombo->addItem(tr("Aggregate"), FunctionManager::Function::AGGREGATE);

    new UserInputFilter(ui->functionFilterEdit, this, SLOT(applyFilter(QString)));
    functionFilterModel->setFilterCaseSensitivity(Qt::CaseInsensitive);

    initActions();

    connect(ui->list->selectionModel(), SIGNAL(selectionChanged(QItemSelection,QItemSelection)), this, SLOT(functionSelected(QItemSelection,QItemSelection)));
    connect(ui->list->selectionModel(), SIGNAL(selectionChanged(QItemSelection,QItemSelection)), this, SLOT(updateState()));
    connect(ui->mainCodeEdit, SIGNAL(textChanged()), this, SLOT(updateModified()));
    connect(ui->nameEdit, SIGNAL(textChanged(QString)), this, SLOT(validateName()));
    connect(ui->nameEdit, SIGNAL(textChanged(QString)), this, SLOT(updateModified()));
    connect(ui->undefArgsCheck, SIGNAL(clicked()), this, SLOT(updateModified()));
    connect(ui->allDatabasesRadio, SIGNAL(clicked()), this, SLOT(updateModified()));
    connect(ui->selDatabasesRadio, SIGNAL(clicked()), this, SLOT(updateModified()));
    connect(ui->langCombo, SIGNAL(currentTextChanged(QString)), this, SLOT(updateModified()));
    connect(ui->typeCombo, SIGNAL(currentTextChanged(QString)), this, SLOT(updateModified()));

    connect(ui->argsList->selectionModel(), SIGNAL(selectionChanged(QItemSelection,QItemSelection)), this, SLOT(updateArgsState()));
    connect(ui->argsList->model(), SIGNAL(rowsMoved(QModelIndex,int,int,QModelIndex,int)), this, SLOT(updateModified()));
    connect(ui->argsList->model(), SIGNAL(dataChanged(QModelIndex,QModelIndex)), this, SLOT(updateModified()));
    connect(ui->argsList->model(), SIGNAL(rowsInserted(QModelIndex,int,int)), this, SLOT(updateModified()));
    connect(ui->argsList->model(), SIGNAL(rowsRemoved(QModelIndex,int,int)), this, SLOT(updateModified()));

    connect(dbListModel, SIGNAL(dataChanged(QModelIndex,QModelIndex)), this, SLOT(updateModified()));

    model->setData(FUNCTIONS->getAllFunctions());

    // Language plugins
    foreach (SqlFunctionPlugin* plugin, PLUGINS->getLoadedPlugins<SqlFunctionPlugin>())
        functionPlugins[plugin->getLanguageName()] = plugin;

    ui->langCombo->addItems(functionPlugins.keys());

    // Syntax highlighting plugins
    foreach (SyntaxHighlighterPlugin* plugin, PLUGINS->getLoadedPlugins<SyntaxHighlighterPlugin>())
        highlighterPlugins[plugin->getLanguageName()] = plugin;

    updateState();
}

int FunctionsEditor::getCurrentFunctionRow() const
{
    QModelIndexList idxList = ui->list->selectionModel()->selectedIndexes();
    if (idxList.size() == 0)
        return -1;

    return idxList.first().row();
}

void FunctionsEditor::functionDeselected(int row)
{
    model->setName(row, ui->nameEdit->text());
    model->setLang(row, ui->langCombo->currentText());
    model->setType(row, getCurrentFunctionType());
    model->setUndefinedArgs(row, ui->undefArgsCheck->isChecked());
    model->setAllDatabases(row, ui->allDatabasesRadio->isChecked());
    model->setCode(row, ui->mainCodeEdit->toPlainText());
    model->setModified(row, currentModified);

    if (model->isAggregate(row))
        model->setFinalCode(row, ui->finalCodeEdit->toPlainText());
    else
        model->setFinalCode(row, QString::null);

    if (!ui->undefArgsCheck->isChecked())
        model->setArguments(row, getCurrentArgList());

    if (ui->selDatabasesRadio->isChecked())
        model->setDatabases(row, getCurrentDatabases());

    model->validateNames();
}

void FunctionsEditor::functionSelected(int row)
{
    ui->nameEdit->setText(model->getName(row));
    ui->mainCodeEdit->setPlainText(model->getCode(row));
    ui->finalCodeEdit->setPlainText(model->getFinalCode(row));
    ui->undefArgsCheck->setChecked(model->getUndefinedArgs(row));
    ui->langCombo->setCurrentText(model->getLang(row));

    // Arguments
    ui->argsList->clear();
    QListWidgetItem* item;
    foreach (const QString& arg, model->getArguments(row))
    {
        item = new QListWidgetItem(arg);
        item->setFlags(item->flags() | Qt::ItemIsEditable);
        ui->argsList->addItem(item);
    }

    // Databases
    dbListModel->setDatabases(model->getDatabases(row));
    ui->databasesList->expandAll();

    if (model->getAllDatabases(row))
        ui->allDatabasesRadio->setChecked(true);
    else
        ui->selDatabasesRadio->setChecked(true);

    // Type
    FunctionManager::Function::Type type = model->getType(row);
    for (int i = 0; i < ui->typeCombo->count(); i++)
    {
        if (ui->typeCombo->itemData(i).toInt() == type)
        {
            ui->typeCombo->setCurrentIndex(i);
            break;
        }
    }

    currentModified = false;

    updateCurrentFunctionState();
}

void FunctionsEditor::clearEdits()
{
    ui->nameEdit->setText(QString::null);
    ui->mainCodeEdit->setPlainText(QString::null);
    ui->langCombo->setCurrentText(QString::null);
    ui->undefArgsCheck->setChecked(true);
    ui->argsList->clear();
    ui->allDatabasesRadio->setChecked(true);
    ui->typeCombo->setCurrentIndex(0);
    ui->langCombo->setCurrentIndex(-1);
}

void FunctionsEditor::selectFunction(int row)
{
    if (!model->isValidRow(row))
        return;

    ui->list->selectionModel()->setCurrentIndex(model->index(row), QItemSelectionModel::Clear|QItemSelectionModel::SelectCurrent);
}


QModelIndex FunctionsEditor::getSelectedArg() const
{
    QModelIndexList indexes = ui->argsList->selectionModel()->selectedIndexes();
    if (indexes.size() == 0 || !indexes.first().isValid())
        return QModelIndex();

    return indexes.first();

}

QStringList FunctionsEditor::getCurrentArgList() const
{
    QStringList currArgList;
    for (int row = 0; row < ui->argsList->model()->rowCount(); row++)
        currArgList << ui->argsList->item(row)->text();

    return currArgList;
}

QStringList FunctionsEditor::getCurrentDatabases() const
{
    return dbListModel->getDatabases();
}

FunctionManager::Function::Type FunctionsEditor::getCurrentFunctionType() const
{
    int intValue = ui->typeCombo->itemData(ui->typeCombo->currentIndex()).toInt();
    return static_cast<FunctionManager::Function::Type>(intValue);
}

void FunctionsEditor::commit()
{
    int row = getCurrentFunctionRow();
    if (model->isValidRow(row))
        functionDeselected(row);

    QList<FunctionManager::FunctionPtr> functions = model->getFunctions();

    FUNCTIONS->setFunctions(functions);
    model->clearModified();
    currentModified = false;

    if (model->isValidRow(row))
        selectFunction(row);

    updateState();
}

void FunctionsEditor::rollback()
{
    int selectedBefore = getCurrentFunctionRow();

    model->setData(FUNCTIONS->getAllFunctions());
    currentModified = false;
    clearEdits();

    if (model->isValidRow(selectedBefore))
        selectFunction(selectedBefore);

    updateState();
}

void FunctionsEditor::newFunction()
{
    if (ui->langCombo->currentIndex() == -1 && ui->langCombo->count() > 0)
        ui->langCombo->setCurrentIndex(0);

    FunctionManager::FunctionPtr func = FunctionManager::FunctionPtr::create();
    func->name = generateUniqueName("function", model->getFunctionNames());

    if (ui->langCombo->currentIndex() > -1)
        func->lang = ui->langCombo->currentText();

    model->addFunction(func);

    selectFunction(model->rowCount() - 1);
}

void FunctionsEditor::deleteFunction()
{
    int row = getCurrentFunctionRow();
    model->deleteFunction(row);
    clearEdits();

    row = getCurrentFunctionRow();
    if (model->isValidRow(row))
        functionSelected(row);

    updateState();
}

void FunctionsEditor::updateModified()
{
    int row = getCurrentFunctionRow();
    if (model->isValidRow(row))
    {
        bool nameDiff = model->getName(row) != ui->nameEdit->text();
        bool codeDiff = model->getCode(row) != ui->mainCodeEdit->toPlainText();
        bool langDiff = model->getLang(row) != ui->langCombo->currentText();
        bool undefArgsDiff = model->getUndefinedArgs(row) != ui->undefArgsCheck->isChecked();
        bool allDatabasesDiff = model->getAllDatabases(row) != ui->allDatabasesRadio->isChecked();
        bool argDiff = getCurrentArgList() != model->getArguments(row);
        bool dbDiff = getCurrentDatabases().toSet() != model->getDatabases(row).toSet(); // QSet to ignore order
        bool typeDiff = model->getType(row) != getCurrentFunctionType();

        currentModified = (nameDiff || codeDiff || typeDiff || langDiff || undefArgsDiff || allDatabasesDiff || argDiff || dbDiff);
    }

    updateState();
}

void FunctionsEditor::updateState()
{
    bool modified = model->isModified() || currentModified;

    actionMap[COMMIT]->setEnabled(modified);
    actionMap[ROLLBACK]->setEnabled(modified);
    actionMap[DELETE]->setEnabled(ui->list->selectionModel()->selectedIndexes().size() > 0);

    updateCurrentFunctionState();
}

void FunctionsEditor::updateCurrentFunctionState()
{
    int row = getCurrentFunctionRow();

    QString name = model->getName(row);
    bool nameOk = !name.isNull();

    setValidStyle(ui->langLabel, true);

    ui->rightWidget->setEnabled(nameOk);
    if (!nameOk)
        return;

    bool langOk = ui->langCombo->currentIndex() >= 0;
    ui->mainCodeGroup->setEnabled(langOk);
    ui->finalCodeGroup->setEnabled(langOk);
    ui->argsGroup->setEnabled(langOk);
    ui->databasesGroup->setEnabled(langOk);
    ui->nameEdit->setEnabled(langOk);
    ui->nameLabel->setEnabled(langOk);
    ui->typeCombo->setEnabled(langOk);
    ui->typeLabel->setEnabled(langOk);
    setValidStyle(ui->langLabel, langOk);

    bool aggregate = getCurrentFunctionType() == FunctionManager::Function::AGGREGATE;
    ui->finalCodeGroup->setVisible(aggregate);
    ui->mainCodeGroup->setTitle(aggregate ? tr("Per step code:") : tr("Function implementation code:"));

    ui->databasesList->setEnabled(ui->selDatabasesRadio->isChecked());

    // Syntax highlighter
    QString lang = ui->langCombo->currentText();
    if (lang != currentHighlighterLang)
    {
        if (currentMainHighlighter)
        {
            // A little pointers swap to local var - this is necessary, cause deleting highlighter
            // triggers textChanged on QPlainTextEdit, which then calls this method,
            // so it becomes an infinite recursion with deleting the same pointer.
            // We set the pointer to null first, then delete it. That way it's safe.
            QSyntaxHighlighter* highlighter = currentMainHighlighter;
            currentMainHighlighter = nullptr;
            delete highlighter;
        }

        if (currentFinalHighlighter)
        {
            QSyntaxHighlighter* highlighter = currentFinalHighlighter;
            currentFinalHighlighter = nullptr;
            delete highlighter;
        }

        if (langOk && highlighterPlugins.contains(lang))
        {
            currentMainHighlighter = highlighterPlugins[lang]->createSyntaxHighlighter(ui->mainCodeEdit);
            currentFinalHighlighter = highlighterPlugins[lang]->createSyntaxHighlighter(ui->finalCodeEdit);
        }

        currentHighlighterLang = lang;
    }

    updateArgsState();
}

void FunctionsEditor::functionSelected(const QItemSelection& selected, const QItemSelection& deselected)
{
    int deselCnt = deselected.indexes().size();
    int selCnt = selected.indexes().size();

    if (deselCnt > 0)
        functionDeselected(deselected.indexes().first().row());

    if (selCnt > 0)
        functionSelected(selected.indexes().first().row());

    if (deselCnt > 0 && selCnt == 0)
    {
        currentModified = false;
        clearEdits();
    }
}

void FunctionsEditor::validateName()
{
    bool valid = model->isAllowedName(getCurrentFunctionRow(), ui->nameEdit->text());
    setValidStyle(ui->nameEdit, valid);
}

void FunctionsEditor::addFunctionArg()
{
    QListWidgetItem* item = new QListWidgetItem(tr("argument", "new function argument name in function editor window"));
    item->setFlags(item->flags () | Qt::ItemIsEditable);
    ui->argsList->addItem(item);

    QModelIndex idx = ui->argsList->model()->index(ui->argsList->model()->rowCount() - 1, 0);
    ui->argsList->selectionModel()->setCurrentIndex(idx, QItemSelectionModel::Clear|QItemSelectionModel::SelectCurrent);

    ui->argsList->editItem(item);
}

void FunctionsEditor::editFunctionArg()
{
    QModelIndex selected = getSelectedArg();
    if (!selected.isValid())
        return;

    int row = selected.row();
    QListWidgetItem* item = ui->argsList->item(row);
    ui->argsList->editItem(item);
}

void FunctionsEditor::delFunctionArg()
{
    QModelIndex selected = getSelectedArg();
    if (!selected.isValid())
        return;

    int row = selected.row();
    delete ui->argsList->takeItem(row);
}

void FunctionsEditor::moveFunctionArgUp()
{
    QModelIndex selected = getSelectedArg();
    if (!selected.isValid())
        return;

    int row = selected.row();
    if (row <= 0)
        return;

    ui->argsList->insertItem(row - 1, ui->argsList->takeItem(row));

    QModelIndex idx = ui->argsList->model()->index(row - 1, 0);
    ui->argsList->selectionModel()->setCurrentIndex(idx, QItemSelectionModel::Clear|QItemSelectionModel::SelectCurrent);
}

void FunctionsEditor::moveFunctionArgDown()
{
    QModelIndex selected = getSelectedArg();
    if (!selected.isValid())
        return;

    int row = selected.row();
    if (row >= ui->argsList->model()->rowCount() - 1)
        return;

    ui->argsList->insertItem(row + 1, ui->argsList->takeItem(row));

    QModelIndex idx = ui->argsList->model()->index(row + 1, 0);
    ui->argsList->selectionModel()->setCurrentIndex(idx, QItemSelectionModel::Clear|QItemSelectionModel::SelectCurrent);
}

void FunctionsEditor::updateArgsState()
{
    bool argsEnabled = !ui->undefArgsCheck->isChecked();
    QModelIndexList indexes = ui->argsList->selectionModel()->selectedIndexes();
    bool argSelected = indexes.size() > 0;

    bool canMoveUp = false;
    bool canMoveDown = false;
    if (argSelected)
    {
        canMoveUp = indexes.first().row() > 0;
        canMoveDown = (indexes.first().row() + 1) < ui->argsList->count();
    }

    actionMap[ARG_ADD]->setEnabled(argsEnabled);
    actionMap[ARG_EDIT]->setEnabled(argsEnabled && argSelected);
    actionMap[ARG_DEL]->setEnabled(argsEnabled && argSelected);
    actionMap[ARG_MOVE_UP]->setEnabled(argsEnabled && canMoveUp);
    actionMap[ARG_MOVE_DOWN]->setEnabled(argsEnabled && canMoveDown);
    ui->argsList->setEnabled(argsEnabled);
}

void FunctionsEditor::applyFilter(const QString& value)
{
    // Remembering old selection, clearing it and restoring afterwards is a workaround for a problem,
    // which causees application to crash, when the item was selected, but after applying filter string,
    // item was about to disappear.
    // This must have something to do with the underlying model (FunctionsEditorModel) implementation,
    // but for now I don't really know what is that.
    // I have tested simple Qt application with the same routine, but the underlying model was QStandardItemModel
    // and everything worked fine.
    int row = getCurrentFunctionRow();
    ui->list->selectionModel()->clearSelection();

    functionFilterModel->setFilterFixedString(value);

    selectFunction(row);
}

QVariant FunctionsEditor::saveSession()
{
    return QVariant();
}

FunctionsEditor::DbModel::DbModel(QObject* parent) :
    QSortFilterProxyModel(parent)
{
}

QVariant FunctionsEditor::DbModel::data(const QModelIndex& index, int role) const
{
    if (role != Qt::CheckStateRole)
        return QSortFilterProxyModel::data(index, role);

    DbTreeItem* item = getItemForProxyIndex(index);
    if (!item)
        return QSortFilterProxyModel::data(index, role);

    DbTreeItem::Type type = item->getType();
    if (type != DbTreeItem::Type::DB && type != DbTreeItem::Type::INVALID_DB)
        return QSortFilterProxyModel::data(index, role);

    return checkedDatabases.contains(item->text(), Qt::CaseInsensitive) ? Qt::Checked : Qt::Unchecked;
}

bool FunctionsEditor::DbModel::setData(const QModelIndex& index, const QVariant& value, int role)
{
    if (role != Qt::CheckStateRole)
        return QSortFilterProxyModel::setData(index, value, role);

    DbTreeItem* item = getItemForProxyIndex(index);
    if (!item)
        return QSortFilterProxyModel::setData(index, value, role);

    DbTreeItem::Type type = item->getType();
    if (type != DbTreeItem::Type::DB && type != DbTreeItem::Type::INVALID_DB)
        return QSortFilterProxyModel::setData(index, value, role);

    if (value.toBool())
        checkedDatabases << item->text();
    else
        checkedDatabases.removeOne(item->text());

    emit dataChanged(index, index, {Qt::CheckStateRole});

    return true;
}

Qt::ItemFlags FunctionsEditor::DbModel::flags(const QModelIndex& index) const
{
    return QSortFilterProxyModel::flags(index) | Qt::ItemIsUserCheckable;
}

void FunctionsEditor::DbModel::setDatabases(const QStringList& databases)
{
    beginResetModel();
    checkedDatabases = databases;
    endResetModel();
}

QStringList FunctionsEditor::DbModel::getDatabases() const
{
    return checkedDatabases;
}

bool FunctionsEditor::DbModel::filterAcceptsRow(int srcRow, const QModelIndex& srcParent) const
{
    QModelIndex idx = sourceModel()->index(srcRow, 0, srcParent);
    DbTreeItem* item = dynamic_cast<DbTreeItem*>(dynamic_cast<DbTreeModel*>(sourceModel())->itemFromIndex(idx));
    switch (item->getType())
    {
        case DbTreeItem::Type::DIR:
        case DbTreeItem::Type::DB:
        case DbTreeItem::Type::INVALID_DB:
            return true;
        default:
            return false;
    }
    return false;
}

DbTreeItem* FunctionsEditor::DbModel::getItemForProxyIndex(const QModelIndex& index) const
{
    QModelIndex srcIdx = mapToSource(index);
    DbTreeItem* item = dynamic_cast<DbTreeItem*>(dynamic_cast<DbTreeModel*>(sourceModel())->itemFromIndex(srcIdx));
    return item;
}