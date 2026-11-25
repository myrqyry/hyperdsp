#pragma once
#include <QWidget>
#include <QJsonObject>
#include <QJsonArray>
#include <QPointer>
#include <vector>
#include <map>
#include <QPointF>
class QDoubleSpinBox;class QPushButton;class QComboBox;class QLineEdit;class QListWidget;class QGraphicsView;class QGraphicsScene;class QLabel;class QFormLayout;class QTableWidget;class QScrollArea;
namespace hyperdsp {
class EffectBuilder final:public QWidget {
public:
    explicit EffectBuilder(QWidget* parent=nullptr);
    QJsonObject draft()const{return draft_;}
    void setDraft(const QJsonObject&);
    void refreshTheme();
protected:
    void changeEvent(QEvent*) override;
private:
    QJsonObject draft_;
    QJsonArray inventory_;
    QJsonArray inventory()const;
    int selected_=-1;
    std::map<int,QPointF> positions_;
    QComboBox *target_,*output_;
    QLineEdit* search_;
    QListWidget* library_;
    QGraphicsView* graph_;
    QGraphicsScene* scene_;
    QFormLayout* parameters_;
    QTableWidget* edges_;
    QComboBox *macros_=nullptr,*macroTarget_=nullptr;
    QLineEdit* macroName_=nullptr;
    QDoubleSpinBox* macroDefault_=nullptr;
    QTableWidget* macroBindings_=nullptr;
    QPushButton *addMacro_=nullptr,*removeMacro_=nullptr,*addBinding_=nullptr,*removeBinding_=nullptr;
    void refreshMacros();
    void inspectMacro();
    void changeMacro(const QString&,const QJsonValue&);
    void addMacro();
    void removeMacro();
    void addBinding();
    void removeBinding();
    QJsonObject bindingMetadata(int node,const QString& key)const;
    QLabel* status_=nullptr;
    bool statusError_=false;
    void refreshTargets();
    void refreshLibrary();
    void redraw();
    void redrawWires();
    void fitCanvas();
    void connectPorts(int from,int to);
    void disconnectCable(int to,int row);
    bool acceptsEdge(int from,int to,int ignoreRow=-1)const;
    void updateNodePosition(int id,QPointF pos);
    void inspect();
    void addNode();
    void addEdge();
    void removeEdge();
    void removeNode();
    void apply(bool validateOnly);
    void loadTarget();
    void replaceNode(const QJsonObject&);
    QJsonObject metadata(const QString&)const;
    void message(const QString&,bool error=false);
};
void register_effect_builder();
void unregister_effect_builder();
}
