#include <functions.hpp>

#include <QDebug>
#include <QQmlListProperty>
#include "listmodel.hpp"

namespace qmlwrap
{

ListModel::ListModel(const cxx_wrap::ArrayRef<jl_value_t*>& array, jl_function_t* f, QObject* parent) : QAbstractListModel(parent), m_array(array), m_update_array(f)
{
  m_rolenames[0] = "string";
  m_getters.push_back(cxx_wrap::JuliaFunction("string").pointer());
  m_setters.push_back(nullptr);
  cxx_wrap::protect_from_gc(m_array.wrapped());
  if(f != nullptr)
  {
    cxx_wrap::protect_from_gc(f);
  }
}

ListModel::~ListModel()
{
  cxx_wrap::unprotect_from_gc(m_array.wrapped());
  if(m_update_array != nullptr)
  {
    cxx_wrap::unprotect_from_gc(m_update_array);
  }

  for(jl_function_t* f : m_getters)
  {
    cxx_wrap::unprotect_from_gc(f);
  }

  for(jl_function_t* f : m_setters)
  {
    if(f != nullptr)
    {
      cxx_wrap::unprotect_from_gc(f);
    }
  }
}

int	ListModel::rowCount(const QModelIndex& parent) const
{
  return m_array.size();
}

QVariant ListModel::data(const QModelIndex& index, int role) const
{
  if(index.row() < 0 || index.row() >= m_array.size())
  {
    qWarning() << "Row index " << index << " is out of range for ListModel";
    return QVariant();
  }
  QVariant result = cxx_wrap::convert_to_cpp<QVariant>(rolegetter(role)(m_array[index.row()]));
  return result;
}

QHash<int, QByteArray> ListModel::roleNames() const
{
  return m_rolenames;
}

bool ListModel::setData(const QModelIndex& index, const QVariant& value, int role)
{
  if(index.row() < 0 || index.row() >= m_array.size())
  {
    qWarning() << "Row index " << index << " is out of range for ListModel";
    return false;
  }

  try
  {
    rolesetter(role)((jl_value_t*)m_array.wrapped(), cxx_wrap::box(value), index.row()+1);
    do_update(index.row(), 1, QVector<int>() << role);
    return true;
  }
  catch(const std::runtime_error&)
  {
    qWarning() << "Null setter for role " << m_rolenames[role] << ", not changing value";
    return false;
  }
}

Qt::ItemFlags ListModel::flags(const QModelIndex&) const
{
  return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;
}

void ListModel::append_list(const QVariantList& argvariants)
{
  if(m_constructor == nullptr)
  {
    qWarning() << "No constructor function set, cannot append item to ListModel";
    return;
  }

  const int nb_args = argvariants.size();

  jl_value_t* result = nullptr;
  jl_value_t** julia_args;
  JL_GC_PUSH1(&result);
  JL_GC_PUSHARGS(julia_args, nb_args);
  for(int i = 0; i != nb_args; ++i)
  {
    julia_args[i] = cxx_wrap::convert_to_julia(argvariants[i]);
  }

  result = jl_call(m_constructor, julia_args, nb_args);
  if(result == nullptr)
  {
    qWarning() << "Error appending ListModel element " << argvariants << ", did you define all required roles for the constructor?";
    JL_GC_POP();
    JL_GC_POP();
    return;
  }

  beginInsertRows(QModelIndex(), m_array.size(), m_array.size());

  m_array.push_back(result);

  do_update();
  JL_GC_POP();
  JL_GC_POP();
  endInsertRows();
  emit countChanged();
}

void ListModel::append(const QJSValue& record)
{
  if(record.isArray())
  {
    append_list(record.toVariant().toList());
    return;
  }

  QVariantList argvariants;
  const int nb_roles = m_rolenames.size();
  for(int i =0; i != nb_roles; ++i)
  {
    auto rolename = m_rolenames[i];
    if(record.hasProperty(rolename))
    {
      argvariants.push_back(record.property(QString(rolename)).toVariant());
    }
  }
  append_list(argvariants);
}

Q_INVOKABLE void ListModel::insert(int index, const QJSValue& record)
{
  append(record);
  move(m_array.size()-1, index, 1);
}

Q_INVOKABLE void ListModel::insert_list(int index, const QVariantList& argvariants)
{
  append_list(argvariants);
  move(m_array.size()-1, index, 1);
}

void ListModel::setProperty(int index, const QString& property, const QVariant& value)
{
  setData(createIndex(index,0), value, m_rolenames.key(property.toUtf8()));
}

void ListModel::remove(int index)
{
  if(index < 0 || index >= m_array.size())
  {
    qWarning() << "Row index " << index << " is out of range for ListModel";
    return;
  }

  beginRemoveRows(QModelIndex(), index, index);

  int nb_elems = m_array.size();
  for(int i = index; i != nb_elems-1; ++i)
  {
    m_array[i] = m_array[i+1];
  }

  jl_array_del_end(m_array.wrapped(), 1);

  do_update();

  endRemoveRows();
  emit countChanged();
}

void ListModel::move(int from, int to, int count)
{
  if(from == to || count == 0)
    return;

  if(to < from)
  {
    const int c = from - to;
    std::swap(from, to);
    to = from + count;
    count = c;
  }

  if(from < 0 || from >= m_array.size() || to < 0 || to >= m_array.size() || to+count > m_array.size())
  {
    qWarning() << "Invalid indexing for move in ListModel";
    return;
  }

  beginMoveRows(QModelIndex(), from, from + count - 1, QModelIndex(), to+count);

  jl_value_t** removed_elems;
  JL_GC_PUSHARGS(removed_elems, count);

  // Save elements to move
  for(int i = 0; i != count; ++i)
  {
    removed_elems[i] = m_array[from+i];
  }
  // Shift elements that remain
  for(int i = from; i != to; ++i)
  {
    m_array[i] = m_array[i+count];
  }
  // Place saved elements in to block
  for(int i = 0; i != count; ++i)
  {
    m_array[to+i] = removed_elems[i];
  }

  do_update();
  JL_GC_POP();
  endMoveRows();
}

void ListModel::clear()
{
  beginRemoveRows(QModelIndex(), 0, m_array.size());
  jl_array_del_end(m_array.wrapped(), m_array.size());
  do_update();
  endRemoveRows();
}

int ListModel::count() const
{
  return m_array.size();
}

void ListModel::addrole(const std::string& name, jl_function_t* getter, jl_function_t* setter)
{
  if(m_rolenames.values().contains(name.c_str()))
  {
    qWarning() << "Role " << name.c_str() << "exists, aborting add";
    return;
  }

  if(getter == nullptr)
  {
    qWarning() << "Invalid getter for role " << name.c_str() << ", aborting add";
    return;
  }

  if(!m_custom_roles)
  {
    m_rolenames.clear();
    m_getters.clear();
    m_setters.clear();
    m_custom_roles = true;
  }

  cxx_wrap::protect_from_gc(getter);
  if(setter != nullptr)
  {
    cxx_wrap::protect_from_gc(setter);
  }

  m_rolenames[m_rolenames.size()] = name.c_str();
  m_getters.push_back(getter);
  m_setters.push_back(setter);

  emit rolesChanged();
}

void ListModel::setrole(const int idx, const std::string& name, jl_function_t* getter, jl_function_t* setter)
{
  if(idx >= m_getters.size() || idx < 0)
  {
    qWarning() << "Listmodel index " << idx << " is out of range, aborting setrole";
    return;
  }
  if(m_rolenames.values().contains(name.c_str()) && m_rolenames.key(name.c_str()) != idx)
  {
    qWarning() << "Role " << name.c_str() << "exists, aborting setrole";
    return;
  }

  if(getter == nullptr)
  {
    qWarning() << "Invalid getter for role " << name.c_str() << ", aborting setrole";
    return;
  }

  cxx_wrap::unprotect_from_gc(m_getters[idx]);
  if(m_setters[idx] != nullptr)
  {
    cxx_wrap::unprotect_from_gc(m_setters[idx]);
  }

  cxx_wrap::protect_from_gc(getter);
  if(setter != nullptr)
  {
    cxx_wrap::protect_from_gc(setter);
  }

  m_getters[idx] = getter;
  m_setters[idx] = setter;
  if(m_rolenames[idx] == name.c_str())
  {
    emit dataChanged(createIndex(0, 0), createIndex(m_array.size() - 1, 0), QVector<int>() << idx);
  }
  else
  {
    m_rolenames[idx] = name.c_str();
    emit rolesChanged();
  }
}

void ListModel::removerole(const int idx)
{
  if(!m_rolenames.contains(idx))
  {
    qWarning() << "Request to delete non-existing role, aborting";
    return;
  }

  cxx_wrap::unprotect_from_gc(m_getters[idx]);
  if(m_setters[idx] != nullptr)
  {
    cxx_wrap::unprotect_from_gc(m_setters[idx]);
  }

  const int nb_roles = m_getters.size();
  for(int i = idx; i != (nb_roles-1); ++i)
  {
    m_getters[i] = m_getters[i+1];
    m_setters[i] = m_setters[i+1];
    m_rolenames[i] = m_rolenames[i+1];
  }
  m_getters.resize(nb_roles-1);
  m_setters.resize(nb_roles-1);
  m_rolenames.remove(nb_roles-1);

  emit rolesChanged();
}

void ListModel::removerole(const std::string& name)
{
  if(!m_rolenames.values().contains(name.c_str()))
  {
    qWarning() << "rolename " << name.c_str() << " not found, aborting removerole";
    return;
  }
  removerole(m_rolenames.key(name.c_str()));
}

void ListModel::setconstructor(jl_function_t* constructor)
{
  m_constructor = constructor;
  cxx_wrap::protect_from_gc(m_constructor);
}

cxx_wrap::JuliaFunction ListModel::rolegetter(int role) const
{
  if(role < 0 || role >= m_rolenames.size())
  {
    qWarning() << "Role index " << role << " is out of range for ListModel, defaulting to string conversion";
    return cxx_wrap::JuliaFunction("string");
  }

  return cxx_wrap::JuliaFunction(m_getters[role]);
}

cxx_wrap::JuliaFunction ListModel::rolesetter(int role) const
{
  if(role < 0 || role >= m_rolenames.size())
  {
    qWarning() << "Role index " << role << " is out of range for ListModel, returning null setter";
  }

  return cxx_wrap::JuliaFunction(m_setters[role]);
}

void ListModel::do_update(int index, int count, const QVector<int> &roles)
{
  do_update();
  emit dataChanged(createIndex(index, 0), createIndex(index + count - 1, 0), roles);
}

void ListModel::do_update()
{
  if(m_update_array != nullptr)
  {
    jl_call0(m_update_array);
  }
}

QStringList ListModel::roles() const
{
  const int nb_roles = m_rolenames.size();
  QStringList rolelist;
  for(int i = 0; i != nb_roles; ++i)
  {
    rolelist.push_back(m_rolenames[i]);
  }
  return rolelist;
}

} // namespace qmlwrap
