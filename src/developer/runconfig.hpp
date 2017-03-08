/*!
 * \file
 */
#ifndef RUNCONFIG_HPP
#define RUNCONFIG_HPP

#include <QObject>

namespace Developer {

class RunConfig : public QObject
{
    Q_OBJECT

public:
    RunConfig(QObject *parent = 0, const QString& name = QString(""), const QString& program = QString(""), const QString& graph = QString(""));
    ~RunConfig();
    
		QString name();
		QString program();
		QString graph();

		bool getTracing();
		bool getBacktracking();

		void setName(QString name);
		void setProgram(QString program);
		void setGraph(QString graph);

		void setTracing(bool tracing);
		void setBacktracking(bool backtracking);
        void setProgramTracing(bool programTracing);

		bool hasTracing();
		bool hasBacktracking();
        bool hasProgramTracing();


signals:
    
public slots:

private:
		QString _name;
		QString _graph;
		QString _program;
		bool _tracing;
        bool _backtracking;

        /** "Program tracing", meaning the option to trace the GP2 program at
         * runtime into a .gptrace file to use in the Tracing tab of the editor. */
        bool _programTracing;
};

}

#endif // RUNCONFIG_HPP
