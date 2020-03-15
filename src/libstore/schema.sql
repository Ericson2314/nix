create table if not exists ValidPaths (
    id               integer primary key autoincrement not null,
    path             text unique not null,
    hash             text not null,
    registrationTime integer not null,
    deriver          text,
    narSize          integer,
    ultimate         integer, -- null implies "false"
    sigs             text, -- space-separated
    ca               text -- if not null, an assertion that the path is content-addressed; see ValidPathInfo
);

create table if not exists Refs (
    referrer  integer not null,
    reference integer not null,
    primary key (referrer, reference),
    foreign key (referrer) references ValidPaths(id) on delete cascade,
    foreign key (reference) references ValidPaths(id) on delete restrict
);

create index if not exists IndexReferrer on Refs(referrer);
create index if not exists IndexReference on Refs(reference);

-- Paths can refer to themselves, causing a tuple (N, N) in the Refs
-- table.  This causes a deletion of the corresponding row in
-- ValidPaths to cause a foreign key constraint violation (due to `on
-- delete restrict' on the `reference' column).  Therefore, explicitly
-- get rid of self-references.
create trigger if not exists DeleteSelfRefs before delete on ValidPaths
  begin
    delete from Refs where referrer = old.id and reference = old.id;
  end;

-- Just used for derivations which are "resolved" in normal form, i.e. those
-- that would not be changed by `hashDerivationModulo`.
create table if not exists DerivationOutputs (
    drv  integer not null,
    id   text not null, -- symbolic output id, usually "out"
    path text not null,
    primary key (drv, id),
    foreign key (drv) references ValidPaths(id) on delete cascade
);

create index if not exists IndexDerivationOutputs on DerivationOutputs(path);

-- maps derivations to their resolved equivalents, if the input derivation isn't
-- itself resolved and the output derivation is known.
create table if not exists ResolvedDrv (
    unresolved_drv integer not null,
    resolved_drv integer not null,
    primary key (unresolved_drv, resolved_drv),
    foreign key (unresolved_drv) references ValidPaths(id) on delete cascade,
    foreign key (resolved_drv) references ValidPaths(id) on delete restrict
)

create trigger if not exists DeleteIdentityDrvResolutions before delete on ValidPaths
  begin
    delete from ResolvedDrv where drv = old.id and resolved_drv = old.id;
  end;
