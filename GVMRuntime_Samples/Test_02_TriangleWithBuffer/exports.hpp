#pragma once
enum class IntExportVariableName
{
};
enum class DoubleExportVariableName
{
};
class AbstractRenderer
{
    public:virtual int& getIntExportVariable(const IntExportVariableName name) = 0;
    virtual void setIntExportVariable(const IntExportVariableName name, int value){int &var = getIntExportVariable(name);var = value;};
    virtual double& getDoubleExportVariable(const DoubleExportVariableName name) = 0;
    virtual void setDoubleExportVariable(const DoubleExportVariableName name, double value){double &var = getDoubleExportVariable(name);var = value;};
    virtual ~AbstractRenderer() = default;
};
