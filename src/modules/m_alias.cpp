/*       +------------------------------------+
 *       | Inspire Internet Relay Chat Daemon |
 *       +------------------------------------+
 *
 *  InspIRCd: (C) 2002-2008 InspIRCd Development Team
 * See: http://www.inspircd.org/wiki/index.php/Credits
 *
 * This program is free but copyrighted software; see
 *            the file COPYING for details.
 *
 * ---------------------------------------------------
 */

#include "inspircd.h"

/* $ModDesc: Provides aliases of commands. */

/** An alias definition
 */
class Alias : public classbase
{
 public:
	/** The text of the alias command */
	irc::string AliasedCommand;

	/** Text to replace with */
	std::string ReplaceFormat;

	/** Nickname required to perform alias */
	std::string RequiredNick;

	/** Alias requires ulined server */
	bool ULineOnly;

	/** Requires oper? */
	bool OperOnly;

	/* is case sensitive params */
	bool CaseSensitive;

	/* whether or not it may be executed via fantasy (default OFF) */
	bool ChannelCommand;

	/* whether or not it may be executed via /command (default ON) */
	bool UserCommand;

	/** Format that must be matched for use */
	std::string format;
};

class ModuleAlias : public Module
{
 private:
	/* We cant use a map, there may be multiple aliases with the same name.
	 * We can, however, use a fancy invention: the multimap. Maps a key to one or more values.
	 *		-- w00t
   */
	std::multimap<std::string, Alias> Aliases;

	virtual void ReadAliases()
	{
		ConfigReader MyConf(ServerInstance);

		Aliases.clear();
		for (int i = 0; i < MyConf.Enumerate("alias"); i++)
		{
			Alias a;
			std::string txt;
			txt = MyConf.ReadValue("alias", "text", i);
			a.AliasedCommand = txt.c_str();
			a.ReplaceFormat = MyConf.ReadValue("alias", "replace", i, true);
			a.RequiredNick = MyConf.ReadValue("alias", "requires", i);
			a.ULineOnly = MyConf.ReadFlag("alias", "uline", i);
			a.ChannelCommand = MyConf.ReadFlag("alias", "channelcommand", i);
			a.UserCommand = MyConf.ReadFlag("alias", "usercommand", i);
			a.OperOnly = MyConf.ReadFlag("alias", "operonly", i);
			a.format = MyConf.ReadValue("alias", "format", i);
			a.CaseSensitive = MyConf.ReadFlag("alias", "matchcase", i);
			Aliases.insert(std::make_pair(txt, a));
		}
	}

 public:

	ModuleAlias(InspIRCd* Me)
		: Module(Me)
	{
		ReadAliases();
		Me->Modules->Attach(I_OnPreCommand, this);
		Me->Modules->Attach(I_OnRehash, this);

	}

	virtual ~ModuleAlias()
	{
	}

	virtual Version GetVersion()
	{
		return Version("$Id$", VF_VENDOR,API_VERSION);
	}

	std::string GetVar(std::string varname, const std::string &original_line)
	{
		irc::spacesepstream ss(original_line);
		varname.erase(varname.begin());
		int index = *(varname.begin()) - 48;
		varname.erase(varname.begin());
		bool everything_after = (varname == "-");
		std::string word;

		for (int j = 0; j < index; j++)
			ss.GetToken(word);

		if (everything_after)
		{
			std::string more;
			while (ss.GetToken(more))
			{
				word.append(" ");
				word.append(more);
			}
		}

		return word;
	}

	void SearchAndReplace(std::string& newline, const std::string &find, const std::string &replace)
	{
		std::string::size_type x = newline.find(find);
		while (x != std::string::npos)
		{
			newline.erase(x, find.length());
			newline.insert(x, replace);
			x = newline.find(find);
		}
	}

	virtual int OnPreCommand(std::string &command, std::vector<std::string> &parameters, User *user, bool validated, const std::string &original_line)
	{
		std::multimap<std::string, Alias>::iterator i;

		/* If theyre not registered yet, we dont want
		 * to know.
		 */
		if (user->registered != REG_ALL)
			return 0;

		/* We dont have any commands looking like this? Stop processing. */
		i = Aliases.find(command);
		if (i == Aliases.end())
			return 0;

		irc::string c = command.c_str();
		/* The parameters for the command in their original form, with the command stripped off */
		std::string compare = original_line.substr(command.length());
		while (*(compare.c_str()) == ' ')
			compare.erase(compare.begin());

		std::string safe(original_line);

		/* Escape out any $ symbols in the user provided text */

		SearchAndReplace(safe, "$", "\r");

		while (i != Aliases.end())
		{
			if (i->second.UserCommand)
				DoAlias(user, &(i->second), compare, safe);

			i++;
		}

		// If aliases have been processed, aliases took it.
		return 1;
	}

	void DoAlias(User *user, Alias *a, const std::string compare, const std::string safe)
	{
		User *u = NULL;
		/* Does it match the pattern? */
		if (!a->format.empty())
		{
			if (a->CaseSensitive)
			{
				if (InspIRCd::Match(compare, a->format, case_sensitive_map))
					return;
			}
			else
			{
				if (InspIRCd::Match(compare, a->format))
					return;
			}
		}

		if ((a->OperOnly) && (!IS_OPER(user)))
			return;

		if (!a->RequiredNick.empty())
		{
			u = ServerInstance->FindNick(a->RequiredNick);
			if (!u)
			{
				user->WriteNumeric(401, ""+std::string(user->nick)+" "+a->RequiredNick+" :is currently unavailable. Please try again later.");
				return;
			}
		}
		if ((u != NULL) && (!a->RequiredNick.empty()) && (a->ULineOnly))
		{
			if (!ServerInstance->ULine(u->server))
			{
				ServerInstance->SNO->WriteToSnoMask('A', "NOTICE -- Service "+a->RequiredNick+" required by alias "+std::string(a->AliasedCommand.c_str())+" is not on a u-lined server, possibly underhanded antics detected!");
				user->WriteNumeric(401, ""+std::string(user->nick)+" "+a->RequiredNick+" :is an imposter! Please inform an IRC operator as soon as possible.");
				return;
			}
		}

		/* Now, search and replace in a copy of the original_line, replacing $1 through $9 and $1- etc */

		std::string::size_type crlf = a->ReplaceFormat.find('\n');

		if (crlf == std::string::npos)
		{
			DoCommand(a->ReplaceFormat, user, safe);
			return;
		}
		else
		{
			irc::sepstream commands(a->ReplaceFormat, '\n');
			std::string scommand;
			while (commands.GetToken(scommand))
			{
				DoCommand(scommand, user, safe);
			}
			return;
		}
	}

	void DoCommand(std::string newline, User* user, const std::string &original_line)
	{
		std::vector<std::string> pars;

		for (int v = 1; v < 10; v++)
		{
			std::string var = "$";
			var.append(ConvToStr(v));
			var.append("-");
			std::string::size_type x = newline.find(var);

			while (x != std::string::npos)
			{
				newline.erase(x, var.length());
				newline.insert(x, GetVar(var, original_line));
				x = newline.find(var);
			}

			var = "$";
			var.append(ConvToStr(v));
			x = newline.find(var);

			while (x != std::string::npos)
			{
				newline.erase(x, var.length());
				newline.insert(x, GetVar(var, original_line));
				x = newline.find(var);
			}
		}

		/* Special variables */
		SearchAndReplace(newline, "$nick", user->nick);
		SearchAndReplace(newline, "$ident", user->ident);
		SearchAndReplace(newline, "$host", user->host);
		SearchAndReplace(newline, "$vhost", user->dhost);

		/* Unescape any variable names in the user text before sending */
		SearchAndReplace(newline, "\r", "$");

		irc::tokenstream ss(newline);
		pars.clear();
		std::string command, token;

		ss.GetToken(command);
		while (ss.GetToken(token) && (pars.size() <= MAXPARAMETERS))
		{
			pars.push_back(token);
		}
		ServerInstance->Parser->CallHandler(command, pars, user);
	}

	virtual void OnRehash(User* user, const std::string &parameter)
	{
		ReadAliases();
 	}
};

MODULE_INIT(ModuleAlias)
