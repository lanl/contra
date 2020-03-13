#ifndef CONTRA_SYMBOLS_HPP
#define CONTRA_SYMBOLS_HPP

#include "identifier.hpp"
#include "sourceloc.hpp"
#include "vartype.hpp"

#include <map>
#include <string>

namespace contra {
  
//==============================================================================
// The base symbol type
//==============================================================================
class TypeDef {

  std::string Name_;

public:

  TypeDef(const std::string & Name) : Name_(Name) {}

  virtual ~TypeDef() = default;

  virtual const std::string & getName() const { return Name_; }
  virtual bool isNumber() const { return false; }
};


//==============================================================================
// The builtin symbol type
//==============================================================================
class BuiltInTypeDef : public TypeDef {
  bool IsNumber_ = false;
public:

  BuiltInTypeDef(const std::string & Name, bool IsNumber=false)
    : TypeDef(Name), IsNumber_(IsNumber) {}

  virtual ~BuiltInTypeDef() = default;
  virtual bool isNumber() const override { return IsNumber_; }

};

//==============================================================================
// The builtin symbol type
//==============================================================================
class UserTypeDef : public TypeDef {
  SourceLocation Loc_;
public:

  UserTypeDef(const std::string & Name, const SourceLocation & Loc) : TypeDef(Name),
    Loc_(Loc) {}

  virtual ~UserTypeDef() = default;

  virtual const SourceLocation & getLoc() const { return Loc_; }

};

//==============================================================================
// The variable type
//==============================================================================
class VariableType {

  std::shared_ptr<TypeDef> Type_;
  bool IsArray_ = false;

public:

  VariableType() = default;

  explicit VariableType(std::shared_ptr<TypeDef> Type, bool IsArray = false)
    : Type_(Type), IsArray_(IsArray)
  {}

  //virtual ~VariableType() = default;

  const std::shared_ptr<TypeDef> getBaseType() const { return Type_; }

  bool isArray() const { return IsArray_; }
  void setArray(bool IsArray=true) { IsArray_ = IsArray; }

  bool isNumber() const { return (!IsArray_ && Type_->isNumber()); }

  bool isCastableTo(const VariableType &To) const { return false; }

  bool isAssignableTo(const VariableType &LeftType) const
  {
    if (!LeftType.isArray() && isArray()) return false;
    return isCastableTo(LeftType);
  }

  bool operator==(const VariableType & other)
  { return Type_ == other.Type_ && IsArray_ == other.IsArray_; }
  bool operator!=(const VariableType & other)
  { return Type_ != other.Type_ || IsArray_ != other.IsArray_; }
  
  operator bool() const { return static_cast<bool>(Type_); }

  friend std::ostream &operator<<( std::ostream &out, const VariableType &obj )
  {
    if (obj.IsArray_) out << "[";
     out << obj.Type_->getName();
    if (obj.IsArray_) out << "]";
     return out;
  }
};

using VariableTypeList = std::vector<VariableType>;

//==============================================================================
// The variable symbol
//==============================================================================
class VariableDef : public Identifier, public VariableType {

public:

  VariableDef(const std::string & Name, const SourceLocation & Loc, 
      std::shared_ptr<TypeDef> Type, bool IsArray = false)
    : VariableType(Type, IsArray), Identifier(Name, Loc)
  {}

  VariableDef(const std::string & Name, const SourceLocation & Loc, 
      const VariableType & VarType)
    : VariableType(VarType), Identifier(Name, Loc)
  {}

  VariableType getType() const { return *this; }

  //virtual ~Variable() = default;

};

//==============================================================================
// The function symbol type
//==============================================================================
class FunctionDef{

public:


protected:

  std::string Name_;
  VariableTypeList ArgTypes_;
  VariableType ReturnType_;

public:

  FunctionDef(const std::string & Name, const VariableTypeList & ArgTypes)
    : Name_(Name), ArgTypes_(ArgTypes), ReturnType_(Context::VoidType)
  {}

  FunctionDef(const std::string & Name, const VariableTypeList & ArgTypes,
      const VariableType & ReturnType)
    : Name_(Name), ArgTypes_(ArgTypes), ReturnType_(ReturnType)
  {}

  //virtual ~FunctionTypeDef() = default;

  const auto & getName() const { return Name_; }
  const auto & getReturnType() const { return ReturnType_; }
  const auto & getArgTypes() const { return ArgTypes_; }
  const auto & getArgType(int i) const { return ArgTypes_[i]; }
  auto getNumArgs() const { return ArgTypes_.size(); }
};


//==============================================================================
// The function symbol type
//==============================================================================
class BuiltInFunction : public FunctionDef {

public:

  BuiltInFunction(const std::string & Name, const VariableTypeList & ArgTypes)
    : FunctionDef(Name, ArgTypes)
  {}

  BuiltInFunction(const std::string & Name, const VariableTypeList & ArgTypes,
      const VariableType & ReturnType) : FunctionDef(Name, ArgTypes, ReturnType)
  {}

};


//==============================================================================
// The function symbol type
//==============================================================================
class UserFunction : public FunctionDef {

  SourceLocation Loc_;

public:

  UserFunction(const std::string & Name, const SourceLocation & Loc,
      const VariableTypeList & ArgTypes)
    : FunctionDef(Name, ArgTypes), Loc_(Loc)
  {}

  UserFunction(const std::string & Name, const SourceLocation & Loc,
      const VariableTypeList & ArgTypes, const VariableType & ReturnType)
    : FunctionDef(Name, ArgTypes, ReturnType), Loc_(Loc)
  {}

  //virtual ~FunctionTypeDef() = default;
};

} // namespace

#endif // CONTRA_SYMBOLS_HPP
