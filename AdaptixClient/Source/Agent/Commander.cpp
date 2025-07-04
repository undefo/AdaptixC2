#include <Agent/Commander.h>

QString serializeParam(const QString &token)
{
    QString result = token;
    result.replace("\\", "\\\\");
    result.replace("\"", "\\\"");
    if (result.contains(' ')) {
        result = "\"" + result + "\"";
    }
    return result;
}

QStringList unserializeParams(const QString &commandline)
{
    QStringList tokens;
    QString token;
    bool inQuotes = false;
    int len = commandline.length();

    for (int i = 0; i < len; ) {
        QChar c = commandline[i];

        if (c.isSpace() && !inQuotes) {
            if (!token.isEmpty()) {
                tokens << token;
                token.clear();
            }
            ++i;
            continue;
        }

        // If we encounter a double quote
        if (c == '"') {
            inQuotes = !inQuotes;
            ++i;
            continue;
        }

        // If we encounter a backslash, handle escape sequences
        if (c == '\\') {
            int numBS = 0;
            // Count the number of consecutive backslashes
            while (i < len && commandline[i] == '\\') {
                ++numBS;
                ++i;
            }
            // Check if the next character is a double quote
            if (i < len && commandline[i] == '"') {
                // Append half the number of backslashes (integer division)
                token.append(QString(numBS / 2, '\\'));
                if (numBS % 2 == 0) {
                    // Even number of backslashes: the quote is not escaped, so it toggles the quote state
                    inQuotes = !inQuotes;
                } else {
                    // Odd number of backslashes: the quote is escaped, add it to the token
                    token.append('"');
                }
                ++i;
            } else {
                // No double quote after backslashes: all backslashes are literal
                token.append(QString(numBS, '\\'));
            }
            continue;
        }

        token.append(c);
        ++i;
    }

    if (!token.isEmpty())
        tokens << token;

    return tokens;
}

void BofPacker::Pack(const QString &type, const QJsonValue &jsonValue)
{
    if (type == "CSTR") {
        if (!jsonValue.isString())
            return;

        QByteArray valueData = jsonValue.toString().toUtf8();

        if (valueData.size() == 0) {
            QByteArray valueLengthData;
            int strLength = 0;
            valueLengthData.append(reinterpret_cast<const char*>(&strLength), 4);

            data.append(valueLengthData);
        }
        else {
            valueData.append('\0');

            QByteArray valueLengthData;
            int strLength = valueData.size();
            valueLengthData.append(reinterpret_cast<const char*>(&strLength), 4);

            data.append(valueLengthData);
            data.append(valueData);
        }
    }
    else if (type == "WSTR") {
        if (!jsonValue.isString())
            return;

        QString str = jsonValue.toString();

        if (str.size() == 0) {
            QByteArray valueLengthData;
            int strLength = 0;
            valueLengthData.append(reinterpret_cast<const char*>(&strLength), 4);

            data.append(valueLengthData);
        }
        else {
            const char16_t* utf16Data = reinterpret_cast<const char16_t*>(str.utf16());
            int utf16Length = str.size() + 1;

            QByteArray strData;
            strData.append(reinterpret_cast<const char*>(utf16Data), utf16Length * sizeof(char16_t));

            QByteArray strLengthData;
            int strLength = utf16Length * sizeof(char16_t);
            strLengthData.append(reinterpret_cast<const char*>(&strLength), 4);

            data.append(strLengthData);
            data.append(strData);
        }
    }
    else if (type == "INT") {
        if (jsonValue.isString()) {
            bool ok;
            int num = jsonValue.toString().toInt(&ok);
            if (!ok)
                return;

            QByteArray numData;
            numData.append(reinterpret_cast<const char*>(&num), sizeof(num));
            data.append(numData);
        }
        else if (jsonValue.isDouble()) {
            int num = jsonValue.toDouble();
            QByteArray numData;
            numData.append(reinterpret_cast<const char*>(&num), sizeof(num));
            data.append(numData);
        }
        else if (jsonValue.isBool()) {
            int num = jsonValue.toBool();
            QByteArray numData;
            numData.append(reinterpret_cast<const char*>(&num), sizeof(num));
            data.append(numData);
        }
    }
    else if (type == "SHORT") {
        if (jsonValue.isString()) {
            bool ok;
            short num = jsonValue.toString().toShort(&ok);
            if (!ok)
                return;

            QByteArray numData;
            numData.append(reinterpret_cast<const char*>(&num), sizeof(num));
            data.append(numData);
        }
        else if (jsonValue.isDouble()) {
            short num = jsonValue.toDouble();
            QByteArray numData;
            numData.append(reinterpret_cast<const char*>(&num), sizeof(num));
            data.append(numData);
        }
        else if (jsonValue.isBool()) {
            short num = jsonValue.toBool();
            QByteArray numData;
            numData.append(reinterpret_cast<const char*>(&num), sizeof(num));
            data.append(numData);
        }
    }
    else if (type == "BYTES") {
        if (!jsonValue.isString())
            return;

        QByteArray bytes = QByteArray::fromBase64(jsonValue.toString().toUtf8());

        QByteArray bytesLengthData;
        int bytesLength = bytes.size();
        bytesLengthData.append(reinterpret_cast<const char*>(&bytesLength), 4);

        data.append(bytesLengthData);
        if (bytesLength > 0)
            data.append(bytes);
    }
}

QString BofPacker::Build() const
{
    QByteArray strLengthData;
    int strLength = data.size();
    strLengthData.append(reinterpret_cast<const char*>(&strLength), sizeof(strLength));

    strLengthData.append(data);
    return strLengthData.toBase64();
}



Commander::Commander(){}

Commander::~Commander() = default;

bool Commander::AddRegCommands(const QByteArray &jsonData)
{
    QList<Command> commandsList;

    QJsonParseError parseError;
    QJsonDocument document = QJsonDocument::fromJson(jsonData, &parseError);

    QJsonArray commandArray = document.array();
    for (QJsonValue value : commandArray) {

        QJsonObject jsonObject = value.toObject();
        Command cmd = this->ParseCommand(jsonObject);
        commandsList.append(cmd);
    }

    commands = commandsList;
    return true;
}

bool Commander::AddExtModule(const QString &filepath, const QString &extName, QList<QJsonObject> extCommands, QList<QJsonObject> extConstants)
{
    QList<Command> commandsList;

    for (QJsonObject jsonObject : extCommands) {
        Command extCmd = this->ParseCommand(jsonObject);
        commandsList.append(extCmd);
    }

    QMap<QString, Constant> constantList;
    for (QJsonObject jsonObject : extConstants) {
        Constant constant = this->ParseConstant(jsonObject);
        constantList[constant.Name] = constant;
    }

    ExtModule extMod = {extName, filepath, commandsList, constantList};
    extModules[filepath] = extMod;
    return true;
}

void Commander::RemoveExtModule(const QString &filepath)
{
    extModules.remove(filepath);
}



Constant Commander::ParseConstant(QJsonObject jsonObject)
{
    Constant constant;
    constant.Name = jsonObject["name"].toString();

    if (jsonObject.contains("map")) {
        QJsonObject mapObject = jsonObject["map"].toObject();
        for ( auto it = mapObject.begin(); it != mapObject.end(); ++it )
            constant.Map[it.key()] = it.value().toString();
    }
    return constant;
}

Command Commander::ParseCommand(QJsonObject jsonObject)
{
    Command cmd;
    cmd.name        = jsonObject["command"].toString();
    cmd.message     = jsonObject["message"].toString();
    cmd.description = jsonObject["description"].toString();
    cmd.example     = jsonObject["example"].toString();
    cmd.exec        = jsonObject["exec"].toString();

    if (jsonObject.contains("subcommands")) {
        QJsonArray subcommandArray = jsonObject["subcommands"].toArray();
        for (QJsonValue subCmdVal : subcommandArray) {
            QJsonObject subCmdObj = subCmdVal.toObject();

            Command subCmd;
            subCmd.name        = subCmdObj["name"].toString();
            subCmd.message     = subCmdObj["message"].toString();
            subCmd.description = subCmdObj["description"].toString();
            subCmd.example     = subCmdObj["example"].toString();
            subCmd.exec        = subCmdObj["exec"].toString();

            QJsonArray subArgsArray = subCmdObj["args"].toArray();
            for (QJsonValue subArgVal : subArgsArray) {
                Argument subArg = ParseArgument(subArgVal.toString());
                if (subArg.valid)
                    subCmd.args.append(subArg);
            }
            cmd.subcommands.append(subCmd);
        }
    } else if (jsonObject.contains("args")) {

        QJsonArray argsArray = jsonObject["args"].toArray();
        for (QJsonValue argVal : argsArray) {
            Argument arg = ParseArgument(argVal.toString());
            if (arg.valid)
                cmd.args.append(arg);
        }
    }
    return cmd;
}

Argument Commander::ParseArgument(const QString &argString)
{
    Argument arg = {0};
    QRegularExpression regex(R"((\w+)\s+([\[\<][^\s\]]+[\s\w-]*[\>\]])(\s*\([^\)]*\))?(?:\s+\{([\s\S]+)\})?)");
    QRegularExpressionMatch match = regex.match(argString);

    if ( !match.hasMatch()) {
        error = "arguments not parsed";
        arg.valid = false;
        return arg;
    }

    arg.type             = match.captured(1);
    QString flagAndValue = match.captured(2).trimmed();
    QString defaultValue = match.captured(3).trimmed();
    arg.description      = match.captured(4).trimmed();

    if( !defaultValue.isEmpty() ) {
        arg.defaultUsed = true;
        arg.defaultValue = defaultValue.mid(1, defaultValue.size() - 2).trimmed();
    }

    if (flagAndValue.startsWith("<") && flagAndValue.endsWith(">")) {
        arg.required = true;
    }
    else if (flagAndValue.startsWith("[") && flagAndValue.endsWith("]")) {
        arg.required = false;
    }
    else {
        error = "argument must be in <> or []";
        arg.valid = false;
        return arg;
    }

    int spaceIndex = flagAndValue.indexOf(' ');
    if (spaceIndex != -1) {
        arg.mark = flagAndValue.mid(1, spaceIndex - 1).trimmed();
        arg.name = flagAndValue.mid(spaceIndex + 1, flagAndValue.size() - spaceIndex - 2).trimmed();
        arg.flag = true;
    }
    else {
        QString value = flagAndValue.mid(1, flagAndValue.size() - 2).trimmed();
        if( value.startsWith("-") || value.startsWith("/") ) {
            arg.mark = value;
            arg.flag = true;
        }
        else {
            arg.name = value;
        }
    }
    arg.valid = true;
    return arg;
}

CommanderResult Commander::ProcessInput(AgentData agentData, QString input)
{
    QStringList parts = unserializeParams(input);
    if (parts.isEmpty())
        return CommanderResult{true, "", false};

    QString commandName = parts[0];
    parts.removeAt(0);

    if( commandName == "help") {
        return this->ProcessHelp(parts);
    }

    for (Command command : commands) {
        if (command.name == commandName) {
            return ProcessCommand(agentData, command, parts, {});
        }
    }

    for ( auto extMod : extModules ) {
        for (Command command : extMod.Commands) {
            if (command.name == commandName) {
                return ProcessCommand(agentData, command, parts, extMod);
            }
        }
    }

    return CommanderResult{true, "Command not found", true};
}

CommanderResult Commander::ProcessCommand(AgentData agentData, Command command, QStringList args, ExtModule extMod)
{
    QString execStr = "";
    QList<Argument> execArgs;

    QJsonObject jsonObj;
    jsonObj["command"] = command.name;

    if ( command.subcommands.size() == 0 ) {

        QMap<QString, QString> parsedArgsMap;

        QString wideKey;
        for (int i = 0; i < args.size(); ++i) {
            QString arg = args[i];

            bool isWideArgs = true;

            for (Argument commandArg : command.args) {
                if (commandArg.flag) {
                    if (commandArg.type == "BOOL" && commandArg.mark == arg ) {
                        parsedArgsMap[commandArg.mark] = "true";
                        wideKey = commandArg.mark;
                        isWideArgs = false;
                        break;
                    }
                    else if ( commandArg.mark == arg && args.size() > i+1 ) {
                        ++i;
                        parsedArgsMap[commandArg.name] = args[i];
                        wideKey = commandArg.name;
                        isWideArgs = false;
                        break;
                    }
                }
                else if (!parsedArgsMap.contains(commandArg.name)) {
                    parsedArgsMap[commandArg.name] = arg;
                    wideKey = commandArg.name;
                    isWideArgs = false;
                    break;
                }
            }

            if( isWideArgs ) {
                QString wideStr;
                for(int j = i; j < args.size(); ++j) {
                    wideStr += " " + args[j];
                }
                parsedArgsMap[wideKey] += wideStr;
                break;
            }
        }

        for (Argument commandArg : command.args) {
            if (parsedArgsMap.contains(commandArg.name) || parsedArgsMap.contains(commandArg.mark)) {
                if (commandArg.type == "STRING") {
                    jsonObj[commandArg.name] = parsedArgsMap[commandArg.name];
                } else if (commandArg.type == "INT") {
                    jsonObj[commandArg.name] = parsedArgsMap[commandArg.name].toInt();
                } else if (commandArg.type == "BOOL") {
                    jsonObj[commandArg.mark] = parsedArgsMap[commandArg.mark] == "true";
                } else if (commandArg.type == "FILE") {
                    QString path = parsedArgsMap[commandArg.name];
                    if (path.startsWith("~/"))
                        path = QDir::home().filePath(path.mid(2));

                    QFile file(path);
                    if (file.open(QIODevice::ReadOnly)) {
                        QByteArray fileData = file.readAll();
                        jsonObj[commandArg.name] = QString::fromLatin1(fileData.toBase64());
                        file.close();
                    } else {
                        return CommanderResult{true, "Failed to open file: " + path, true };
                    }
                }
            } else if (commandArg.required) {
                if (commandArg.defaultValue.isEmpty() && !commandArg.defaultUsed) {
                    return CommanderResult{true, "Missing required argument: " + commandArg.name, true };
                } else {
                    if (commandArg.type == "STRING") {
                        jsonObj[commandArg.name] = commandArg.defaultValue;
                    } else if (commandArg.type == "INT") {
                        jsonObj[commandArg.name] = commandArg.defaultValue.toInt();
                    } else if (commandArg.type == "BOOL") {
                        jsonObj[commandArg.mark] = commandArg.defaultValue == "true";
                    } else if (commandArg.type == "FILE") {
                        QString path = commandArg.defaultValue;
                        if (path.startsWith("~/"))
                            path = QDir::home().filePath(path.mid(2));

                        QFile file(path);
                        if (file.open(QIODevice::ReadOnly)) {
                            QByteArray fileData = file.readAll();
                            jsonObj[commandArg.name] = QString::fromLatin1(fileData.toBase64());
                            file.close();
                        } else {
                            return CommanderResult{true, "Failed to open file: " + path, true };
                        }
                    }
                }
            }
        }

        QString msg = command.message;
        if( !msg.isEmpty() ) {
            for ( QString k : parsedArgsMap.keys() ) {
                QString param = "<" + k + ">";
                if( msg.contains(param) )
                    msg = msg.replace(param, parsedArgsMap[k]);
            }
            jsonObj["message"] = msg;
        }

        execStr = command.exec;
        execArgs = command.args;
    }
    else {
        if ( args.isEmpty() )
            return CommanderResult{true, "Subcommand must be set", true };

        QString subCommandName = args[0];

        for (Command subcommand : command.subcommands) {
            if (subCommandName == subcommand.name) {
                jsonObj["subcommand"] = subcommand.name;

                QMap<QString, QString> parsedArgsMap;

                QString wideKey;
                for (int i = 1; i < args.size(); ++i) {
                    QString arg = args[i];

                    bool isWideArgs = true;

                    for (Argument commandArg : subcommand.args) {
                        if (commandArg.flag) {
                            if (commandArg.type == "BOOL" && commandArg.mark == arg ) {
                                parsedArgsMap[commandArg.mark] = "true";
                                wideKey = commandArg.mark;
                                isWideArgs = false;
                                break;
                            }
                            else if ( commandArg.mark == arg && args.size() > i+1 ) {
                                ++i;
                                parsedArgsMap[commandArg.name] = args[i];
                                wideKey = commandArg.name;
                                isWideArgs = false;
                                break;
                            }
                        }
                        else if (!parsedArgsMap.contains(commandArg.name)) {
                            parsedArgsMap[commandArg.name] = arg;
                            wideKey = commandArg.name;
                            isWideArgs = false;
                            break;
                        }
                    }

                    if( isWideArgs ) {
                        QString wideStr;
                        for(int j = i; j < args.size(); ++j) {
                            wideStr += " " + args[j];
                        }
                        parsedArgsMap[wideKey] += wideStr;
                        break;
                    }
                }

                for (Argument subArg : subcommand.args) {
                    if (parsedArgsMap.contains(subArg.name) || parsedArgsMap.contains(subArg.mark)) {
                        if (subArg.type == "STRING") {
                            jsonObj[subArg.name] = parsedArgsMap[subArg.name];
                        } else if (subArg.type == "INT") {
                            jsonObj[subArg.name] = parsedArgsMap[subArg.name].toInt();
                        } else if (subArg.type == "BOOL") {
                            jsonObj[subArg.mark] = parsedArgsMap[subArg.mark] == "true";
                        } else if (subArg.type == "FILE") {
                            QString path = parsedArgsMap[subArg.name];
                            if (path.startsWith("~/"))
                                path = QDir::home().filePath(path.mid(2));

                            QFile file(path);
                            if (file.open(QIODevice::ReadOnly)) {
                                QByteArray fileData = file.readAll();
                                jsonObj[subArg.name] = QString::fromLatin1(fileData.toBase64());
                                file.close();
                            } else {
                                return CommanderResult{true, "Failed to open file: " + path, true };
                            }
                        }
                    } else if (subArg.required) {
                        if (subArg.defaultValue.isEmpty() && !subArg.defaultUsed) {
                            return CommanderResult{true, "Missing required argument for subcommand: " + subArg.name, true };
                        } else {
                            if (subArg.type == "STRING") {
                                jsonObj[subArg.name] = subArg.defaultValue;
                            } else if (subArg.type == "INT") {
                                jsonObj[subArg.name] = subArg.defaultValue.toInt();
                            } else if (subArg.type == "BOOL") {
                                jsonObj[subArg.mark] = subArg.defaultValue == "true";
                            } else if (subArg.type == "FILE") {
                                QString path = subArg.defaultValue;
                                if (path.startsWith("~/"))
                                    path = QDir::home().filePath(path.mid(2));

                                QFile file(path);
                                if (file.open(QIODevice::ReadOnly)) {
                                    QByteArray fileData = file.readAll();
                                    jsonObj[subArg.name] = QString::fromLatin1(fileData.toBase64());
                                    file.close();
                                } else {
                                    return CommanderResult{true, "Failed to open file: " + path, true };
                                }
                            }
                        }
                    }
                }

                QString msg = subcommand.message;
                if( !msg.isEmpty() ) {
                    for ( QString k : parsedArgsMap.keys() ) {
                        QString param = "<" + k + ">";
                        if( msg.contains(param) )
                            msg = msg.replace(param, parsedArgsMap[k]);
                    }
                    jsonObj["message"] = msg;
                }

                execStr  = subcommand.exec;
                execArgs = subcommand.args;

                break;
            }
        }
    }

    if( !execStr.isEmpty() ) {
        QString newInput = this->ProcessExecExtension( agentData, extMod, execStr, execArgs, jsonObj);
        CommanderResult execCommandResult = this->ProcessInput(agentData, newInput);
        if( !execCommandResult.error ) {
            QJsonParseError parseError;
            QJsonDocument document = QJsonDocument::fromJson(execCommandResult.message.toUtf8(), &parseError);
            QJsonObject jsonObject = document.object();
            jsonObject["message"] = jsonObj["message"];
            QJsonDocument jsonDoc(jsonObject);
            execCommandResult.message = jsonDoc.toJson();
        }
        return execCommandResult;
    }

    QJsonDocument jsonDoc(jsonObj);
    return CommanderResult{false, jsonDoc.toJson(), false };
}

QString Commander::ProcessExecExtension(const AgentData &agentData, ExtModule extMod, QString ExecString, QList<Argument> args, QJsonObject jsonObj)
{
    /// $ARCH

    ExecString = ExecString.replace("$ARCH()", agentData.Arch, Qt::CaseSensitive);

    /// $EXT_DIR

    QFileInfo fi(extMod.FilePath);
    QString dirPath = fi.absolutePath();
    ExecString = ExecString.replace("$EXT_DIR()", dirPath, Qt::CaseSensitive);

    /// $MAP()

    QRegularExpression mapRe(R"(\$MAP\(\s*(\w+)\s*,\s*(\w+)\s*\))");
    QRegularExpressionMatchIterator mapReIt = mapRe.globalMatch(ExecString);
    while (mapReIt.hasNext()) {
        QRegularExpressionMatch match = mapReIt.next();
        QString mapName = match.captured(1);
        QString key = match.captured(2);

        QString value = "";
        if (extMod.Constants.contains(mapName)) {
            if (extMod.Constants[mapName].Map.contains(key))
                value = extMod.Constants[mapName].Map[key];

        }

        if (!value.isEmpty())
            ExecString = ExecString.replace(match.captured(0), value);
    }

    /// $RAND

    QRegularExpression re(R"(\$RAND\(\s*(\d+)\s*,\s*(\w+)\s*\))");
    QRegularExpressionMatchIterator i = re.globalMatch(ExecString);
    while (i.hasNext()) {
        QRegularExpressionMatch match = i.next();
        int length = match.captured(1).toInt();
        QString setName = match.captured(2);
        QString randomString = GenerateRandomString(length, setName);
        if (!randomString.isEmpty())
            ExecString = ExecString.replace(match.captured(0), randomString);
    }

    /// $HASH

    QRegularExpression hashRe(R"(\$HASH\(\s*(\w+)\s*,\s*(\d+)\s*,\s*([^)]+)\s*\))");
    QRegularExpressionMatchIterator hashIt = hashRe.globalMatch(ExecString);
    while (hashIt.hasNext()) {
        QRegularExpressionMatch match = hashIt.next();
        QString algorithm = match.captured(1);
        int length = match.captured(2).toInt();
        QString inputString = match.captured(3).trimmed();
        //If arguments
        QRegularExpression remainingArgsRegex(R"(\{\s*([^}]*)\s*\})");
        QRegularExpressionMatchIterator remainingIt = remainingArgsRegex.globalMatch(inputString);
        while (remainingIt.hasNext()) {
            QRegularExpressionMatch remainingMatch = remainingIt.next();
            QString paramName = remainingMatch.captured(1).trimmed();
            if( jsonObj.contains(paramName) && jsonObj[paramName].isString() ){
                QString paramValue = serializeParam(jsonObj[paramName].toString());
                inputString = inputString.replace(remainingMatch.captured(0), paramValue);
            }
        }

        QString hashString = GenerateHash(algorithm, length, inputString);
        if (!hashString.isEmpty())
            ExecString = ExecString.replace(match.captured(0), hashString);
    }

    /// BOF_PACK

    QRegularExpression packRegex(R"(\$PACK_BOF\s*\(([^)]*)\))");
    QRegularExpressionMatchIterator iter = packRegex.globalMatch(ExecString);

    while (iter.hasNext()) {
        QRegularExpressionMatch match = iter.next();
        QString packContent = match.captured(1); /// $PACK(...)

        QRegularExpression paramRegex(R"((\s*([A-Z]+)\s+)?(?:\{\s*([^}]*)\s*\}|([^,\s][^,]*[^,\s])))");
        QRegularExpressionMatchIterator it = paramRegex.globalMatch(packContent);

        BofPacker packer;
        while (it.hasNext()) {
            QRegularExpressionMatch paramMatch = it.next();

            QString type = paramMatch.captured(2);
            if (type.isEmpty())
                type = "CSTR";

            if (!paramMatch.captured(3).isEmpty()) {
                QString value = paramMatch.captured(3); /// {param}
                if( jsonObj.contains(value) )
                    packer.Pack( type, jsonObj[value] );
            }
            else if (!paramMatch.captured(4).isEmpty()) {
                QString value = paramMatch.captured(4); /// param
                packer.Pack( type, QJsonValue(value) );
            }
        }
        QString bofParam = packer.Build();
        ExecString = ExecString.replace(match.captured(0), bofParam);
    }

    /// Arguments

    QRegularExpression remainingArgsRegex(R"(\{\s*([^}]*)\s*\})");
    QRegularExpressionMatchIterator remainingIt = remainingArgsRegex.globalMatch(ExecString);

    while (remainingIt.hasNext()) {
        QRegularExpressionMatch remainingMatch = remainingIt.next();
        QString paramName = remainingMatch.captured(1).trimmed();
        if( jsonObj.contains(paramName) && jsonObj[paramName].isString() ){
            QString paramValue = serializeParam(jsonObj[paramName].toString());
            ExecString = ExecString.replace(remainingMatch.captured(0), paramValue);
        }
    }

    return ExecString;
}



QString Commander::GetError()
{
    return error;
}

CommanderResult Commander::ProcessHelp(QStringList commandParts)
{
    QString result;
    QTextStream output(&result);
    if (commandParts.isEmpty()) {
        int TotalWidth = 24;
        output << QString("\n");
        output << QString("  Command                       Description\n");
        output << QString("  -------                       -----------\n");

        for ( auto command : commands ) {
            QString commandName = command.name;
            if (!command.subcommands.isEmpty())
                commandName += '*';

            QString tab = QString(TotalWidth - commandName.size(), ' ');
            output << "  " + commandName + tab + "      " + command.description + "\n";
        }

        for ( auto extMod : extModules.values() ){
            output << QString("\n");
            output << QString("  Extension - " + extMod.Name + "\n");
            output << QString("  =====================================\n");

            for ( auto command : extMod.Commands ) {
                QString commandName = command.name;
                if ( command.subcommands.isEmpty() ) {
                    QString tab = QString(TotalWidth - commandName.size(), ' ');
                    output << "  " + commandName + tab + "      " + command.description + "\n";
                }
                else {
                    for ( auto subcmd : command.subcommands ) {
                        QString subcmdName = commandName + " " + subcmd.name;
                        QString tab = QString(TotalWidth - subcmdName.size(), ' ');
                        output << "  " + subcmdName + tab + "      " + subcmd.description + "\n";
                    }
                }
            }
        }

        return CommanderResult{true, result, false};
    }
    else {
        Command foundCommand;
        QString commandName = commandParts[0];

        for (Command cmd : commands) {
            if (cmd.name == commandName) {
                foundCommand = cmd;
                break;
            }
        }

        for( auto extMod : extModules.values()) {
            if ( !foundCommand.name.isEmpty() )
                break;

            for (Command cmd : extMod.Commands) {
                if (cmd.name == commandName) {
                    foundCommand = cmd;
                    break;
                }
            }
        }

        if ( foundCommand.name.isEmpty() )
            return CommanderResult{true, "Unknown command: " + commandName, true};

        if (commandParts.size() == 1) {
            output << QString("\n");
            output << "  Command               : " + foundCommand.name + "\n";
            if(!foundCommand.description.isEmpty())
                output << "  Description           : " + foundCommand.description + "\n";
            if(!foundCommand.example.isEmpty())
                output << "  Example               : " + foundCommand.example + "\n";
            if( !foundCommand.subcommands.isEmpty() ) {
                output << "\n";
                output << "  SubCommand                Description\n";
                output << "  ----------                -----------\n";
                for ( auto subcmd : foundCommand.subcommands ) {
                    int TotalWidth = 20;
                    int cmdWidth = subcmd.name.size();
                    if (cmdWidth > TotalWidth)
                        cmdWidth = TotalWidth;

                    QString tab = QString(TotalWidth - cmdWidth, ' ');
                    output << "  " + subcmd.name + tab + "      " + subcmd.description + "\n";
                }
            }
            else if (!foundCommand.args.isEmpty()) {
                QString usageHelp;
                QTextStream usageStream(&usageHelp);
                usageStream << foundCommand.name;

                int maxArgLength = 0;
                for (const auto &arg : foundCommand.args) {
                    QString fullarg = (arg.required ? "<" : "[") + arg.mark + (arg.mark.isEmpty() || arg.name.isEmpty() ? "" : " ") + arg.name + (arg.required ? ">" : "]");
                    maxArgLength = qMax(maxArgLength, fullarg.size());
                    usageStream << " " + fullarg;
                }

                output << "  Usage                 : " + usageHelp + "\n\n";
                output << "  Arguments:\n";

                for (const auto &arg : foundCommand.args) {
                    QString fullarg = (arg.required ? "<" : "[") + arg.mark + (arg.mark.isEmpty() || arg.name.isEmpty() ? "" : " ") + arg.name + (arg.required ? ">" : "]");
                    QString padding = QString(maxArgLength - fullarg.size(), ' ');
                    output << "    " + fullarg + padding + "  : " + arg.type + (arg.defaultUsed ? " (default: '" + arg.defaultValue + "'). " : ". ") + arg.description + "\n";
                }
            }
        }
        else if (commandParts.size() == 2) {
            Command foundSubCommand;
            QString subCommandName = commandParts[1];
            for (Command subcmd : foundCommand.subcommands) {
                if (subcmd.name == subCommandName) {
                    foundSubCommand = subcmd;
                    break;
                }
            }

            if ( foundSubCommand.name.isEmpty() )
                return CommanderResult{true, "Unknown subcommand: " + subCommandName, true};

            output << "  Command               : " + foundCommand.name + " " + foundSubCommand.name +"\n";
            if(!foundSubCommand.description.isEmpty())
                output << "  Description           : " + foundSubCommand.description + "\n";
            if(!foundSubCommand.example.isEmpty())
                output << "  Example               : " + foundSubCommand.example + "\n";
            if (!foundSubCommand.args.isEmpty()) {
                QString usageHelp;
                QTextStream usageStream(&usageHelp);
                usageStream << foundCommand.name + " " + foundSubCommand.name;

                int maxArgLength = 0;
                for (const auto &arg : foundSubCommand.args) {
                    QString fullarg = (arg.required ? "<" : "[") + arg.mark + (arg.mark.isEmpty() || arg.name.isEmpty() ? "" : " ") + arg.name + (arg.required ? ">" : "]");
                    maxArgLength = qMax(maxArgLength, fullarg.size());
                    usageStream << " " + fullarg;
                }

                output << "  Usage                 : " + usageHelp + "\n\n";
                output << "  Arguments:\n";

                for (const auto &arg : foundSubCommand.args) {
                    QString fullarg = (arg.required ? "<" : "[") + arg.mark + (arg.mark.isEmpty() || arg.name.isEmpty() ? "" : " ") + arg.name + (arg.required ? ">" : "]");
                    QString padding = QString(maxArgLength - fullarg.size(), ' ');
                    output << "    " + fullarg + padding + "  : " + arg.type + (arg.defaultUsed ? " (default: '" + arg.defaultValue + "'). " : ". ") + arg.description + "\n";
                }
            }
        }
        else {
            return CommanderResult{true, "Error Help format: 'help [command [subcommand]]'", true};
        }
        return CommanderResult{true, output.readAll(), false};
    }
}

QStringList Commander::GetCommands()
{
    QStringList commandList;
    QStringList helpCommandList;

    for (Command cmd : commands) {

        helpCommandList << "help " + cmd.name;
        if (cmd.subcommands.isEmpty())
            commandList << cmd.name;

        for (Command subcmd : cmd.subcommands) {
            commandList << cmd.name + " " + subcmd.name;
            helpCommandList << "help " + cmd.name + " " + subcmd.name;
        }
    }

    for( auto extMod : extModules.values()) {
        for (Command cmd : extMod.Commands) {

            helpCommandList << "help " + cmd.name;
            if (cmd.subcommands.isEmpty())
                commandList << cmd.name;

            for (Command subcmd : cmd.subcommands) {
                commandList << cmd.name + " " + subcmd.name;
                helpCommandList << "help " + cmd.name + " " + subcmd.name;
            }
        }
    }

    for( QString cmd : helpCommandList)
        commandList << cmd;

    return commandList;
}
